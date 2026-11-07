#!/usr/bin/env bash
# Compila el puente del preview web del simulador C.
# El binario sale en build_preview/ (ignorado por git); las fuentes viven aquí.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/build_preview"
mkdir -p "$OUT"

CFLAGS_EXTRA=(-std=c11 -O2 -D_POSIX_C_SOURCE=200809L -Wall -Wextra)

# main.c incluye <SDL2/SDL.h>, así que necesita el padre de includedir,
# no solo el -I.../include/SDL2 que devuelve sdl2-config.
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
  SDL_CFLAGS="$(pkg-config --cflags sdl2)"
  SDL_LIBS="$(pkg-config --libs sdl2)"
  INC_PARENT="$(pkg-config --variable=includedir sdl2)"
  echo "SDL2 via pkg-config"
elif command -v sdl2-config >/dev/null 2>&1; then
  SDL_CFLAGS="$(sdl2-config --cflags)"
  SDL_LIBS="$(sdl2-config --static-libs)"
  INC_PARENT="$(sdl2-config --prefix)/include"
  echo "SDL2 via sdl2-config (PATH)"
elif [ -x "$HOME/.cache/giga-deps/install/bin/sdl2-config" ]; then
  CFG="$HOME/.cache/giga-deps/install/bin/sdl2-config"
  SDL_CFLAGS="$("$CFG" --cflags)"
  SDL_LIBS="$("$CFG" --static-libs)"
  INC_PARENT="$("$CFG" --prefix)/include"
  echo "SDL2 via cache local ($CFG)"
else
  cat >&2 <<'EOF'
ERROR: no se encontró SDL2.
Instálalo (Debian/Ubuntu: apt install libsdl2-dev) o define SDL2_PREFIX.
EOF
  exit 1
fi

# shellcheck disable=SC2086
gcc "${CFLAGS_EXTRA[@]}" -I"$INC_PARENT" $SDL_CFLAGS \
    "$HERE/bridge.c" -o "$OUT/bridge" $SDL_LIBS -lm

echo "Puente compilado: $OUT/bridge"

# Prueba de humo: un fotograma debe traer cabecera + 480*272*2 bytes.
printf 'F\nQ\n' | "$OUT/bridge" > "$OUT/probe.bin"
python3 - "$OUT/probe.bin" <<'PY'
import struct, sys
from pathlib import Path
raw = Path(sys.argv[1]).read_bytes()
expected = 8 + 480 * 272 * 2
if len(raw) != expected:
    sys.exit(f"fotograma inválido: {len(raw)} bytes, esperaba {expected}")
screen, bat = struct.unpack("<II", raw[:8])
print(f"Smoke OK: pantalla={screen} bateria={bat}% bytes={len(raw)}")
PY
