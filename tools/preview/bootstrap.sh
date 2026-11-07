#!/usr/bin/env bash
# Reconstruye las dependencias del preview (CMake, Ninja, SDL2) desde cero.
#
# Por qué existe: ~/.cache/ no sobrevive entre snapshots de la plataforma, así que
# el preview se quedaba sin SDL2 y había que reconstruirlo a mano cada vez. Este
# script es idempotente: si algo ya está, lo reutiliza.
#
# Uso:
#   bash tools/preview/bootstrap.sh              # prefijo por defecto en ~/.cache
#   GIGA_DEPS_PREFIX=/otra/ruta bash ...         # prefijo alternativo (pruebas)
#   GIGA_TOOLS_PREFIX=/otra/ruta bash ...
set -euo pipefail

SDL2_COMMIT="cee83f2d9a916e9de30c5712e798541fdc134b06"   # rama SDL2, verificado
DEPS="${GIGA_DEPS_PREFIX:-$HOME/.cache/giga-deps}"
TOOLS="${GIGA_TOOLS_PREFIX:-$HOME/.cache/giga-tools}"
PREFIX="$DEPS/install"
JOBS="$(nproc 2>/dev/null || echo 2)"

mkdir -p "$DEPS" "$TOOLS"

# --- 1. CMake + Ninja -------------------------------------------------------
# Se prefieren los del sistema; si no hay, se instalan por pip desde PyPI.
if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
  CMAKE="$(command -v cmake)"; NINJA="$(command -v ninja)"
  echo "[1/3] CMake y Ninja del sistema"
else
  export PATH="$TOOLS/bin:$PATH"
  export PYTHONPATH="$TOOLS${PYTHONPATH:+:$PYTHONPATH}"
  if [ ! -x "$TOOLS/bin/cmake" ] || [ ! -x "$TOOLS/bin/ninja" ]; then
    echo "[1/3] Instalando CMake + Ninja en $TOOLS"
    python3 -m pip install --quiet --target "$TOOLS" cmake ninja
  else
    echo "[1/3] CMake + Ninja ya presentes en $TOOLS"
  fi
  CMAKE="$TOOLS/bin/cmake"; NINJA="$TOOLS/bin/ninja"
fi
"$CMAKE" --version | head -1

# --- 2. SDL2 estático -------------------------------------------------------
if [ -f "$PREFIX/lib/libSDL2.a" ] && [ -f "$PREFIX/include/SDL2/SDL.h" ]; then
  echo "[2/3] SDL2 ya instalado en $PREFIX"
else
  echo "[2/3] Construyendo SDL2 ($SDL2_COMMIT) en $PREFIX"
  SRC="$DEPS/SDL2"
  if [ ! -d "$SRC/.git" ]; then
    git init --quiet "$SRC"
    git -C "$SRC" remote add origin https://github.com/libsdl-org/SDL.git 2>/dev/null || true
  fi
  if ! git -C "$SRC" cat-file -e "$SDL2_COMMIT^{commit}" 2>/dev/null; then
    git -C "$SRC" fetch --quiet --depth 1 origin "$SDL2_COMMIT"
  fi
  git -C "$SRC" -c advice.detachedHead=false checkout --quiet "$SDL2_COMMIT"

  # Mismos flags que produjeron el build verificado: estático, Release, sin tests.
  # El resto se autodetecta igual que antes.
  "$CMAKE" -S "$SRC" -B "$DEPS/SDL2-build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
    > "$DEPS/configure.log" 2>&1 || { tail -30 "$DEPS/configure.log"; exit 1; }

  "$CMAKE" --build "$DEPS/SDL2-build" --parallel "$JOBS" \
    > "$DEPS/build.log" 2>&1 || { tail -30 "$DEPS/build.log"; exit 1; }

  "$CMAKE" --install "$DEPS/SDL2-build" \
    > "$DEPS/install.log" 2>&1 || { tail -30 "$DEPS/install.log"; exit 1; }
  echo "      SDL2 listo"
fi

# --- 3. Puente del preview --------------------------------------------------
echo "[3/3] Compilando el puente"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDL2_PREFIX="$PREFIX" bash "$HERE/build.sh"

cat <<EOF

Bootstrap completo. Arrancar el preview con:
  python3 tools/preview/server.py --port 8080
EOF
