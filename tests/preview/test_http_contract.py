"""Contrato HTTP del servidor de preview.

Por que existe: el servidor solo implementaba GET y POST. BaseHTTPRequestHandler
responde 501 a cualquier otro metodo, asi que:

  * HEAD /          -> 501. Los proxies sondan con HEAD para decidir si el
                       servidor vive; con 501 la vista previa se marcaba caida
                       aunque GET / devolviera 200.
  * OPTIONS /input  -> 501. Servido en un iframe de otro origen, el preflight
                       fallaba y el navegador bloqueaba el POST de los toques.

Ademas el protocolo era HTTP/1.0 (conexion cerrada por peticion). Estos tests
cubren los tres puntos mas la reutilizacion de conexion, que es lo que se rompe
si un Content-Length no cuadra.

Se saltan si el puente no esta compilado: bash tools/preview/build.sh
"""
from __future__ import annotations

import http.client
import json
import os
import socket
import subprocess
import sys
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
BRIDGE = ROOT / "build_preview" / "bridge"
SERVER = ROOT / "tools" / "preview" / "server.py"
FRAME_BYTES = 800 * 480 * 2


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@unittest.skipUnless(BRIDGE.exists(), "compila el puente: bash tools/preview/build.sh")
class HttpContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.port = _free_port()
        cls.proc = subprocess.Popen(
            [sys.executable, str(SERVER), "--port", str(cls.port),
             "--bridge", str(BRIDGE)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            cwd=str(ROOT),
        )
        deadline = time.time() + 20
        while time.time() < deadline:
            if cls.proc.poll() is not None:
                raise RuntimeError("el servidor murio al arrancar")
            try:
                with socket.create_connection(("127.0.0.1", cls.port), timeout=0.5):
                    return
            except OSError:
                time.sleep(0.1)
        cls.proc.kill()
        raise RuntimeError("el servidor no escucho a tiempo")

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        try:
            cls.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            cls.proc.kill()
            cls.proc.wait(timeout=5)

    def conn(self):
        return http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)

    def test_head_is_supported_and_sends_no_body(self):
        """HEAD debe dar 200 con las cabeceras de GET y cuerpo vacio."""
        c = self.conn()
        try:
            for path, ctype, size in (
                ("/", "text/html", None),
                ("/health", "text/plain", 2),
                ("/frame.raw", "application/octet-stream", FRAME_BYTES),
            ):
                with self.subTest(path=path):
                    c.request("HEAD", path)
                    r = c.getresponse()
                    body = r.read()
                    self.assertEqual(r.status, 200, "HEAD debe responder 200, no 501")
                    self.assertEqual(body, b"", "HEAD no puede llevar cuerpo")
                    cl = int(r.getheader("Content-Length"))
                    self.assertGreater(cl, 0)
                    if size is not None:
                        self.assertEqual(cl, size)
                    self.assertIn(ctype, r.getheader("Content-Type"))
        finally:
            c.close()

    def test_head_matches_get_content_length(self):
        c = self.conn()
        try:
            c.request("GET", "/")
            r = c.getresponse()
            get_body = r.read()
            get_cl = int(r.getheader("Content-Length"))
            c.request("HEAD", "/")
            r = c.getresponse()
            r.read()
            self.assertEqual(int(r.getheader("Content-Length")), get_cl)
            self.assertEqual(get_cl, len(get_body))
        finally:
            c.close()

    def test_options_preflight_is_allowed(self):
        """Sin OPTIONS el iframe de otro origen no puede enviar los toques."""
        c = self.conn()
        try:
            c.request("OPTIONS", "/input",
                      headers={"Origin": "https://example.invalid",
                               "Access-Control-Request-Method": "POST"})
            r = c.getresponse()
            r.read()
            self.assertIn(r.status, (200, 204))
            self.assertIn("POST", r.getheader("Allow", ""))
            self.assertEqual(r.getheader("Access-Control-Allow-Origin"), "*")
            self.assertIn("Content-Type", r.getheader("Access-Control-Allow-Headers", ""))
        finally:
            c.close()

    def test_frame_headers_are_exposed_cross_origin(self):
        """El JS lee X-Giga-*; sin Expose-Headers un iframe no puede verlas."""
        c = self.conn()
        try:
            c.request("GET", "/frame.raw")
            r = c.getresponse()
            body = r.read()
            self.assertEqual(len(body), FRAME_BYTES)
            exposed = r.getheader("Access-Control-Expose-Headers", "")
            for h in ("X-Giga-Screen", "X-Giga-Battery", "X-Giga-Backlight"):
                self.assertIn(h, exposed)
                self.assertIsNotNone(r.getheader(h), "falta la cabecera " + h)
        finally:
            c.close()

    def test_keep_alive_connection_is_reusable(self):
        """HTTP/1.1: un Content-Length malo desincroniza la conexion.

        Se mezcla HEAD (sin cuerpo) con GET/POST en la MISMA conexion: si HEAD
        escribiera cuerpo o el largo no cuadrara, la siguiente respuesta se
        leeria corrupta.
        """
        c = self.conn()
        try:
            c.request("HEAD", "/")
            r = c.getresponse()
            self.assertEqual(r.status, 200)
            self.assertEqual(r.read(), b"")
            self.assertEqual(r.version, 11, "se espera HTTP/1.1")

            c.request("GET", "/frame.raw")
            r = c.getresponse()
            self.assertEqual(len(r.read()), FRAME_BYTES)

            c.request("POST", "/input",
                      body=json.dumps({"type": "mouse", "x": 388, "y": 180}),
                      headers={"Content-Type": "application/json"})
            r = c.getresponse()
            self.assertEqual(r.status, 200)
            self.assertEqual(r.getheader("X-Giga-Screen"), "2")
            self.assertEqual(len(r.read()), FRAME_BYTES)

            # Un 404 tampoco puede dejar la conexion inservible.
            c.request("GET", "/no-existe")
            r = c.getresponse()
            self.assertEqual(r.status, 404)
            r.read()

            c.request("GET", "/health")
            r = c.getresponse()
            self.assertEqual(r.status, 200)
            self.assertEqual(r.read(), b"ok")
        finally:
            c.close()

    def test_head_does_not_advance_the_simulation(self):
        """Sondear con HEAD no debe consumir un fotograma del puente."""
        c = self.conn()
        try:
            c.request("GET", "/frame.raw")
            r = c.getresponse()
            before = r.read()
            screen_before = r.getheader("X-Giga-Screen")
            for _ in range(5):
                c.request("HEAD", "/frame.raw")
                r = c.getresponse()
                self.assertEqual(r.status, 200)
                self.assertEqual(r.read(), b"")
            c.request("GET", "/frame.raw")
            r = c.getresponse()
            after = r.read()
            self.assertEqual(r.getheader("X-Giga-Screen"), screen_before,
                             "HEAD cambio de pantalla")
            # La barra de estado (reloj/bateria) puede avanzar; el resto no.
            self.assertEqual(after[36 * 1600:], before[36 * 1600:],
                             "HEAD modifico el contenido del panel")
        finally:
            c.close()


if __name__ == "__main__":
    unittest.main()
