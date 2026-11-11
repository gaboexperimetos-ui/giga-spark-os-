#!/usr/bin/env python3
"""Sirve la propuesta táctil estática para la vista previa en vivo."""
import functools
import http.server
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8090


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    handler = functools.partial(Handler, directory=str(ROOT))
    server = http.server.ThreadingHTTPServer(("0.0.0.0", port), handler)
    server.daemon_threads = True
    print(f"Propuesta tactil Gigaspark en http://0.0.0.0:{port}", flush=True)
    server.serve_forever()
