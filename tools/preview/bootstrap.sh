#!/usr/bin/env bash
# Reconstruye las dependencias del preview (CMake, Ninja, SDL3 + SDL2) desde cero.
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

# SDL3 es la version actual (3.4.x). Se instala primero y el simulador la usa
# por defecto. SDL2 2.32.10 queda como respaldo para sistemas que solo traen
# la 2.x (Debian/Ubuntu LTS); ambos producen el mismo framebuffer, verificado.
SDL3_TAG="release-3.4.20"
SDL2_COMMIT="5d249570393f7a37e037abf22cd6012a4cc56a71"   # tag release-2.32.10
DEPS="${GIGA_DEPS_PREFIX:-$HOME/.cache/giga-deps}"
TOOLS="${GIGA_TOOLS_PREFIX:-$HOME/.cache/giga-tools}"
PREFIX="$DEPS/install"
PREFIX3="$DEPS/install3"
JOBS="$(nproc 2>/dev/null || echo 2)"

mkdir -p "$DEPS" "$TOOLS"

# --- 1. CMake + Ninja -------------------------------------------------------
# Se prefieren los del sistema; si no hay, se instalan por pip desde PyPI.
if command -v cmake >/dev/null 2>&1 && command -v ninja >/dev/null 2>&1; then
  CMAKE="$(command -v cmake)"; NINJA="$(command -v ninja)"
  echo "[1/4] CMake y Ninja del sistema"
else
  export PATH="$TOOLS/bin:$PATH"
  export PYTHONPATH="$TOOLS${PYTHONPATH:+:$PYTHONPATH}"
  if [ ! -x "$TOOLS/bin/cmake" ] || [ ! -x "$TOOLS/bin/ninja" ]; then
    echo "[1/4] Instalando CMake + Ninja en $TOOLS"
    python3 -m pip install --quiet --target "$TOOLS" cmake ninja
  else
    echo "[1/4] CMake + Ninja ya presentes en $TOOLS"
  fi
  CMAKE="$TOOLS/bin/cmake"; NINJA="$TOOLS/bin/ninja"
fi
"$CMAKE" --version | head -1

# --- 2. SDL3 estatico (version actual) --------------------------------------
# SDL_UNIX_CONSOLE_BUILD=ON: este entorno no tiene X11/Wayland y SDL3 aborta la
# configuracion si no encuentra ninguno. El preview es headless (renderiza a un
# framebuffer y lo sirve por HTTP), asi que no necesita driver de ventana.
if [ -f "$PREFIX3/lib/libSDL3.a" ] && [ -f "$PREFIX3/include/SDL3/SDL.h" ]; then
  echo "[2/4] SDL3 ya instalado en $PREFIX3"
else
  echo "[2/4] Construyendo SDL3 ($SDL3_TAG) en $PREFIX3"
  SRC3="$DEPS/SDL3"
  if [ ! -d "$SRC3/.git" ]; then
    git init --quiet "$SRC3"
    git -C "$SRC3" remote add origin https://github.com/libsdl-org/SDL.git 2>/dev/null || true
  fi
  if ! git -C "$SRC3" cat-file -e "$SDL3_TAG^{commit}" 2>/dev/null; then
    git -C "$SRC3" fetch --quiet --depth 1 origin "$SDL3_TAG"
    git -C "$SRC3" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
  else
    git -C "$SRC3" -c advice.detachedHead=false checkout --quiet "$SDL3_TAG"
  fi
  "$CMAKE" -S "$SRC3" -B "$DEPS/SDL3-build" -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX3" \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DSDL_UNIX_CONSOLE_BUILD=ON \
    > "$DEPS/configure3.log" 2>&1 || { tail -30 "$DEPS/configure3.log"; exit 1; }
  "$CMAKE" --build "$DEPS/SDL3-build" --parallel "$JOBS" \
    > "$DEPS/build3.log" 2>&1 || { tail -30 "$DEPS/build3.log"; exit 1; }
  "$CMAKE" --install "$DEPS/SDL3-build" \
    > "$DEPS/install3.log" 2>&1 || { tail -30 "$DEPS/install3.log"; exit 1; }
  echo "      SDL3 listo"
fi

# --- 3. SDL2 estatico (respaldo) --------------------------------------------
if [ -f "$PREFIX/lib/libSDL2.a" ] && [ -f "$PREFIX/include/SDL2/SDL.h" ]; then
  echo "[3/4] SDL2 ya instalado en $PREFIX"
else
  echo "[3/4] Construyendo SDL2 ($SDL2_COMMIT) en $PREFIX"
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
echo "[4/4] Compilando el puente"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDL3_PREFIX="$PREFIX3" bash "$HERE/build.sh"

cat <<EOF

Bootstrap completo. Arrancar el preview con:
  python3 tools/preview/server.py --port 8080
EOF
