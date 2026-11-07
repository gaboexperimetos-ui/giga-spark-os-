#!/usr/bin/env python3
"""Vista previa web del simulador C real de Gigaspark OS."""
from __future__ import annotations

import argparse
import json
import struct
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

SCR_W, SCR_H = 800, 480
FRAME_BYTES = SCR_W * SCR_H * 2
HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
DEFAULT_BRIDGE = REPO_ROOT / "build_preview" / "bridge"

HTML = r"""<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Gigaspark OS — Simulador en vivo</title>
<style>
:root { color-scheme: dark; }
* { box-sizing: border-box; }
body { margin: 0; min-height: 100vh; background: radial-gradient(circle at top, #122033, #05080d 70%); color: #e8f1ff; font-family: system-ui, sans-serif; display: flex; flex-direction: column; align-items: center; padding: 24px; }
header { text-align: center; margin-bottom: 18px; }
h1 { margin: 0 0 6px; font-size: clamp(22px, 4vw, 34px); letter-spacing: .04em; }
p { margin: 4px 0; color: #9fb3cc; }
.device { padding: 18px; border-radius: 26px; background: linear-gradient(145deg, #202a36, #0b0f14); box-shadow: 0 24px 70px #000c, inset 0 0 0 2px #33415580; }
canvas { display: block; width: min(92vw, 960px); height: auto; aspect-ratio: 800 / 480; image-rendering: pixelated; background: #000; border-radius: 8px; cursor: pointer; touch-action: none; }
.bar { display: flex; flex-wrap: wrap; gap: 8px; justify-content: center; margin: 18px 0 8px; }
button { background: #142033; color: #dff4ff; border: 1px solid #2d4a66; border-radius: 10px; padding: 9px 13px; font: inherit; cursor: pointer; }
button:hover { background: #1d3550; border-color: #49b6ff; }
.status { margin-top: 10px; font-size: 14px; color: #7fd4ff; }
.hint { max-width: 960px; text-align: center; font-size: 13px; }
</style>
</head>
<body>
<header>
  <h1>GIGASPARK OS</h1>
  <p>Simulador en vivo · pantalla 800×480 renderizada por el código C real</p>
</header>
<div class="device"><canvas id="screen" width="800" height="480"></canvas></div>
<div class="bar">
  <button data-key="Escape">Inicio</button>
  <button data-key="ArrowUp">↑</button>
  <button data-key="ArrowDown">↓</button>
  <button data-key="Enter">Enter</button>
  <button data-key="1">1 Osciloscopio</button>
  <button data-key="2">2 Fuente</button>
  <button data-key="3">3 RF</button>
  <button data-key="4">4 WiFi</button>
  <button data-key="5">5 Ajustes</button>
  <button data-key="6">6 Acerca de</button>
</div>
<div class="status" id="status">Conectando…</div>
<p class="hint">Haz clic en la pantalla para tocar. Usa flechas + Enter, Escape para volver, o las teclas 1–6. El osciloscopio y RF se animan en vivo.</p>
<script>
const SCR_MAX_X = 799, SCR_MAX_Y = 479;
const canvas = document.getElementById('screen');
const ctx = canvas.getContext('2d');
const statusEl = document.getElementById('status');
const names = ['Launcher','Osciloscopio','Fuente de poder','Flipper RF','WiFi Scanner','Ajustes','Acerca de'];
const keymap = { Escape:27, Enter:13, ArrowUp:1073741906, ArrowDown:1073741907, '1':49,'2':50,'3':51,'4':52,'5':53,'6':54 };
let screen = 0, battery = 100, busy = false, lastDraw = 0, lastStatic = 0;

function draw(buf) {
  const px = new Uint16Array(buf);
  const img = ctx.createImageData(800, 480);
  const d = img.data;
  for (let i = 0, j = 0; i < px.length; i++, j += 4) {
    const c = px[i];
    const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    d[j] = (r << 3) | (r >> 2);
    d[j+1] = (g << 2) | (g >> 2);
    d[j+2] = (b << 3) | (b >> 2);
    d[j+3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}
function meta(r) {
  screen = Number(r.headers.get('X-Giga-Screen') || 0);
  battery = Number(r.headers.get('X-Giga-Battery') || 100);
  statusEl.textContent = `${names[screen] || 'Pantalla'} · batería ${battery}% · ${screen===1||screen===3 ? 'animación en vivo' : 'entrada táctil activa'}`;
}
async function pull() {
  const r = await fetch('/frame.raw', { cache: 'no-store' });
  if (!r.ok) throw new Error('frame ' + r.status);
  meta(r); draw(await r.arrayBuffer());
}
async function send(body) {
  const r = await fetch('/input', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body) });
  if (!r.ok) throw new Error('input ' + r.status);
  meta(r); draw(await r.arrayBuffer());
}
async function tick(ts) {
  requestAnimationFrame(tick);
  if (busy) return;
  const animated = screen === 1 || screen === 3;
  const due = animated ? ts - lastDraw >= 33 : ts - lastStatic >= 1000;
  if (!due && lastDraw !== 0) return;
  busy = true;
  try { await pull(); lastDraw = ts; if (!animated) lastStatic = ts; }
  catch (e) { statusEl.textContent = 'Reconectando… ' + e.message; }
  finally { busy = false; }
}
canvas.addEventListener('pointerdown', async e => {
  e.preventDefault();
  const rect = canvas.getBoundingClientRect();
  const x = Math.max(0, Math.min(SCR_MAX_X, Math.floor((e.clientX - rect.left) * 800 / rect.width)));
  const y = Math.max(0, Math.min(SCR_MAX_Y, Math.floor((e.clientY - rect.top) * 480 / rect.height)));
  try { await send({ type:'mouse', x, y }); } catch (e) { statusEl.textContent = e.message; }
});
window.addEventListener('keydown', async e => {
  if (!(e.key in keymap)) return;
  e.preventDefault();
  try { await send({ type:'key', key:e.key }); } catch (err) { statusEl.textContent = err.message; }
});
document.querySelectorAll('button[data-key]').forEach(b => b.addEventListener('click', async () => {
  try { await send({ type:'key', key:b.dataset.key }); } catch (e) { statusEl.textContent = e.message; }
}));
requestAnimationFrame(tick);
</script>
</body>
</html>
"""

KEYMAP = {
    "Escape": 27, "Enter": 13,
    "ArrowUp": 1073741906, "ArrowDown": 1073741907,
    "1": 49, "2": 50, "3": 51, "4": 52, "5": 53, "6": 54,
}


class Bridge:
    def __init__(self, executable: Path):
        self.proc = subprocess.Popen([str(executable)], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, bufsize=0)
        self.lock = threading.Lock()

    def _read_exact(self, n: int) -> bytes:
        chunks = []
        remaining = n
        while remaining:
            chunk = self.proc.stdout.read(remaining)
            if not chunk:
                raise RuntimeError("El puente del simulador terminó")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def command(self, line: str):
        with self.lock:
            self.proc.stdin.write((line + "\n").encode())
            self.proc.stdin.flush()
            header = self._read_exact(8)
            screen, battery = struct.unpack("<II", header)
            return screen, battery, self._read_exact(FRAME_BYTES)


class Handler(BaseHTTPRequestHandler):
    bridge: Bridge

    def log_message(self, fmt, *args):
        pass

    def _send(self, code, ctype, body, extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, str(v))
        self.end_headers()
        self.wfile.write(body)

    def _frame(self, line):
        try:
            screen, battery, raw = self.bridge.command(line)
        except Exception as exc:
            self._send(503, "text/plain; charset=utf-8", str(exc).encode())
            return
        self._send(200, "application/octet-stream", raw,
                   {"X-Giga-Screen": screen, "X-Giga-Battery": battery})

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index.html"):
            self._send(200, "text/html; charset=utf-8", HTML.encode())
        elif self.path.startswith("/frame.raw"):
            self._frame("F")
        elif self.path == "/health":
            self._send(200, "text/plain", b"ok")
        else:
            self._send(404, "text/plain", b"not found")

    def do_POST(self):
        if self.path != "/input":
            self._send(404, "text/plain", b"not found")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length > 4096:
                raise ValueError("too large")
            data = json.loads(self.rfile.read(length) or b"{}")
            if data.get("type") == "key":
                key = data.get("key")
                if key not in KEYMAP:
                    raise ValueError("tecla no permitida")
                line = f"K {KEYMAP[key]}"
            elif data.get("type") == "mouse":
                x = max(0, min(SCR_W - 1, int(data["x"])))
                y = max(0, min(SCR_H - 1, int(data["y"])))
                line = f"M {x} {y}"
            else:
                raise ValueError("entrada no válida")
        except Exception as exc:
            self._send(400, "text/plain; charset=utf-8", str(exc).encode())
            return
        self._frame(line)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--bridge", type=Path, default=DEFAULT_BRIDGE)
    args = parser.parse_args()
    Handler.bridge = Bridge(args.bridge)
    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    server.daemon_threads = True
    print(f"Vista previa Gigaspark OS en http://0.0.0.0:{args.port}", flush=True)
    try:
        server.serve_forever()
    finally:
        Handler.bridge.proc.terminate()


if __name__ == "__main__":
    main()
