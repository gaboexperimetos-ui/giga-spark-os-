#!/usr/bin/env bash
# Compila el puente del preview web del simulador C.
# El binario sale en build_preview/ (ignorado por git); las fuentes viven aquí.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
# GIGA_BUILD_DIR permite construir en un directorio aislado (pruebas del
# bootstrap) sin pisar el binario que usa el preview en ejecución.
OUT="${GIGA_BUILD_DIR:-$ROOT/build_preview}"
mkdir -p "$OUT"

CFLAGS_EXTRA=(-std=c11 -O2 -D_POSIX_C_SOURCE=200809L -Wall -Wextra)

# sdl_compat.h usa <SDL3/SDL.h> o <SDL.h> segun lo que haya. Se prueba SDL3
# primero (version actual) y se cae a SDL2, que sigue siendo el paquete del
# sistema en Debian/Ubuntu LTS.
#
# *_PREFIX tienen prioridad: los usa bootstrap.sh para prefijos aislados.
SDL_MAJOR=0
SDL_CFLAGS=""; SDL_LIBS=""; INC_PARENT=""

try_sdl3() {  # $1 = prefijo (vacio: pkg-config)
  local pfx="${1:-}"
  if [ -n "$pfx" ]; then
    [ -f "$pfx/include/SDL3/SDL.h" ] || return 1
    SDL_CFLAGS="-I$pfx/include"
    SDL_LIBS="$pfx/lib/libSDL3.a -lm -lpthread -ldl"
    INC_PARENT="$pfx/include"
  else
    command -v pkg-config >/dev/null 2>&1 || return 1
    pkg-config --exists sdl3 || return 1
    SDL_CFLAGS="$(pkg-config --cflags sdl3)"
    SDL_LIBS="$(pkg-config --libs --static sdl3)"
    INC_PARENT="$(pkg-config --variable=includedir sdl3)"
  fi
  SDL_MAJOR=3
}

try_sdl2() {  # $1 = prefijo (vacio: pkg-config / sdl2-config)
  local pfx="${1:-}" cfg=""
  if [ -n "$pfx" ]; then
    cfg="$pfx/bin/sdl2-config"
    [ -x "$cfg" ] || return 1
  elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl2; then
    SDL_CFLAGS="$(pkg-config --cflags sdl2)"
    SDL_LIBS="$(pkg-config --libs sdl2)"
    INC_PARENT="$(pkg-config --variable=includedir sdl2)"
    SDL_MAJOR=2; return 0
  elif command -v sdl2-config >/dev/null 2>&1; then
    cfg="$(command -v sdl2-config)"
  elif [ -x "$HOME/.cache/giga-deps/install/bin/sdl2-config" ]; then
    cfg="$HOME/.cache/giga-deps/install/bin/sdl2-config"
  else
    return 1
  fi
  SDL_CFLAGS="$("$cfg" --cflags)"
  SDL_LIBS="$("$cfg" --static-libs)"
  # main.c incluye <SDL.h>: hace falta el padre de include/SDL2.
  INC_PARENT="$("$cfg" --prefix)/include"
  SDL_MAJOR=2
}

if [ -n "${SDL3_PREFIX:-}" ]; then
  try_sdl3 "$SDL3_PREFIX" || { echo "ERROR: SDL3_PREFIX sin include/SDL3/SDL.h" >&2; exit 1; }
elif [ -n "${SDL2_PREFIX:-}" ]; then
  try_sdl2 "$SDL2_PREFIX" || { echo "ERROR: SDL2_PREFIX sin bin/sdl2-config" >&2; exit 1; }
elif try_sdl3 ""; then :
elif try_sdl3 "$HOME/.cache/giga-deps/install3"; then :
elif try_sdl2 ""; then :
else
  cat >&2 <<'EOF'
ERROR: no se encontro SDL3 ni SDL2.
  Debian/Ubuntu: apt install libsdl2-dev
  o reconstruir dependencias: bash tools/preview/bootstrap.sh
  o definir SDL3_PREFIX / SDL2_PREFIX
EOF
  exit 1
fi
echo "Puente usando SDL$SDL_MAJOR"

# shellcheck disable=SC2086
gcc "${CFLAGS_EXTRA[@]}" -DGSP_USE_SDL3=$([ "$SDL_MAJOR" = 3 ] && echo 1 || echo 0) \
    -I"$INC_PARENT" -I"$ROOT/common/include" -I"$ROOT/simulator" $SDL_CFLAGS \
    "$HERE/bridge.c" -o "$OUT/bridge" $SDL_LIBS -lm

echo "Puente compilado: $OUT/bridge"

# Prueba de humo: cabecera de 12 bytes + 800*480*2 de framebuffer.
printf 'F\nQ\n' | "$OUT/bridge" > "$OUT/probe.bin"
python3 - "$OUT/probe.bin" <<'PY'
import struct, sys
from pathlib import Path
raw = Path(sys.argv[1]).read_bytes()
expected = 12 + 800 * 480 * 2
if len(raw) != expected:
    sys.exit(f"fotograma inválido: {len(raw)} bytes, esperaba {expected}")
screen, bat, bl = struct.unpack("<III", raw[:12])
print(f"Smoke OK: pantalla={screen} bateria={bat}% backlight={bl} bytes={len(raw)}")
PY
