#!/usr/bin/env python3
"""Renderiza pantallas del simulador C a PNG para revisarlas sin ventana.

Usa el mismo puente que el preview web, asi que lo que se ve aqui es el
framebuffer real del simulador, no una maqueta.

    python3 tools/preview/shots.py                 # las 7 pantallas
    python3 tools/preview/shots.py --out mis_shots
    python3 tools/preview/shots.py --tap 400 300   # prueba un toque
"""
from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
W, H = 800, 480
NAMES = ["launcher", "osciloscopio", "fuente", "rf", "wifi", "ajustes", "acerca"]


class Bridge:
    def __init__(self, exe: Path):
        self.p = subprocess.Popen([str(exe)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, bufsize=0)

    def _read(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = self.p.stdout.read(n - len(buf))
            if not chunk:
                raise RuntimeError("el puente termino antes de tiempo")
            buf += chunk
        return buf

    def send(self, cmd: str):
        self.p.stdin.write((cmd + "\n").encode())
        self.p.stdin.flush()
        screen, bat = struct.unpack("<II", self._read(8))
        return screen, bat, self._read(W * H * 2)

    def close(self):
        try:
            self.p.stdin.write(b"Q\n")
            self.p.stdin.flush()
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def to_png(raw: bytes, path: Path):
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        base = y * W * 2
        for x in range(W):
            c = raw[base + x * 2] | (raw[base + x * 2 + 1] << 8)
            r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
            rows += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 2), (b << 3) | (b >> 2)))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(rows), 6))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bridge", type=Path, default=ROOT / "build_preview" / "bridge")
    ap.add_argument("--out", type=Path, default=ROOT / "build_preview" / "shots")
    ap.add_argument("--tap", nargs=2, type=int, metavar=("X", "Y"))
    ap.add_argument("--screen", type=int, help="solo esta pantalla (0-6)")
    args = ap.parse_args()

    if not args.bridge.exists():
        sys.exit(f"No existe {args.bridge}. Corre: bash tools/preview/build.sh")
    args.out.mkdir(parents=True, exist_ok=True)

    br = Bridge(args.bridge)
    try:
        if args.tap:
            x, y = args.tap
            screen, _, raw = br.send(f"M {x} {y}")
            path = args.out / f"tap-{x}-{y}.png"
            to_png(raw, path)
            print(f"toque ({x},{y}) -> pantalla {screen}  {path}")
            return
        todo = [args.screen] if args.screen is not None else range(7)
        for i in todo:
            cmd = "F" if i == 0 else f"K {48 + i}"
            screen, bat, raw = br.send(cmd)
            path = args.out / f"{i}-{NAMES[i]}.png"
            to_png(raw, path)
            lit = sum(1 for j in range(0, len(raw), 2) if raw[j] or raw[j + 1])
            print(f"{i} {NAMES[i]:14s} pantalla={screen} bat={bat}% "
                  f"contenido={lit * 100 // (W * H)}%  {path.name}")
    finally:
        br.close()


if __name__ == "__main__":
    main()
