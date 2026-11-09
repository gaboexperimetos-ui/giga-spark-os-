# Cómo contribuir

Gracias por interesarte en Gigaspark OS. Esta guía describe cómo montar el
entorno, qué se verifica antes de aceptar un cambio y las trampas concretas que
ya nos costaron tiempo.

## Qué se puede tocar sin hardware

El proyecto tiene dos mitades y **no se verifican igual**:

| Parte | Se compila en un PC | Estado |
|---|---|---|
| `simulator/`, `common/`, `tests/host/`, `tools/` | Sí, con SDL | Verificado en cada cambio |
| `m7/`, `m4/`, `sdk/` (firmware Zephyr) | Necesita workspace Zephyr + SDK ARM | Solo lo comprueba CI |

Si tu cambio es del firmware, **dilo en la PR**: no podremos ejecutarlo localmente
y dependerá del CI. No afirmes que probaste en una placa si no lo hiciste.

## Entorno para el simulador y los tests

```bash
# Dependencias del sistema (Debian/Ubuntu)
sudo apt-get install cmake ninja-build gcc pkg-config libsdl2-dev

# Tests de host + simulador
cmake -S tests/host -B build_host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_host
ctest --test-dir build_host --output-on-failure

cmake -S simulator -B build_simulator -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_simulator
ctest --test-dir build_simulator --output-on-failure

# Sanitizadores (obligatorio antes de una PR que toque C)
cmake -S tests/host -B build_san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGIGASPARK_SANITIZE=ON
cmake --build build_san
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build_san --output-on-failure
```

SDL3 es la versión actual y se usa si está disponible; SDL2 sirve de respaldo.
Para forzar una ruta: `-DGIGA_FORCE_SDL2=ON` o `make -C simulator SDL=2`.

### Vista previa web (sin ventana ni display)

```bash
bash tools/preview/bootstrap.sh      # construye CMake/Ninja/SDL si faltan
python3 tools/preview/server.py --port 8080
```

`bootstrap.sh` es idempotente: reutiliza lo que ya esté instalado.

## Firmware (Zephyr)

```bash
pip install west
west init -l .
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
west build -b arduino_giga_r1/stm32h747xx/m7 --shield arduino_giga_display_shield
```

Las versiones están fijadas en `west.yml`. Usa el **SDK de Zephyr**
(`arm-zephyr-eabi`), no `gcc-arm-none-eabi` de apt: no es el toolchain que Zephyr
espera y falla al enlazar con picolibc. Consulta `docs/VERSIONS.md`.

## Reglas del código

- **C11**, compilado con `-Wall -Wextra -Werror`. Un warning rompe el build.
- **Indentación con tabs en `.c` y `.h`**; espacios en Python, YAML y CMake.
  Hay un `.editorconfig` que lo aplica solo.
- Comentarios y documentación en español, como el resto del árbol.
- Nada de asignación dinámica en las rutas de dibujo del simulador.

## Qué verificar antes de abrir la PR

1. `ctest` en `build_host`, `build_simulator` y el build con sanitizadores.
2. `python3 -m unittest discover -s tests/preview` — requiere el puente compilado
   (`bash tools/preview/build.sh`); si no, la mitad de la suite **se salta**.
3. Si tocaste la UI: regenera las capturas y **míralas**.

```bash
python3 tools/preview/shots.py     # -> build_preview/shots/*.png
```

Los tests en verde **no detectan layout roto**. Ya pasamos un smoke con botones
invisibles y con texto tapado por controles. Un PNG vale más que un test aquí.

4. Si cambiaste el dibujo a propósito, los hashes de píxeles del baseline no
   van a coincidir. Usa `--allow-visual-change` y **anota los hashes nuevos** en
   `docs/PERFORMANCE.md`:

```bash
python3 tests/bench/compare.py <ref> --runs 5 --allow-visual-change
```

## Trampas que ya nos costaron tiempo

Están documentadas porque volver a caer en ellas es fácil:

- **`cmake --build dirA dirB` no es válido.** CMake responde `Unknown argument` y
  sale con código 1. Si filtras la salida con `grep -E 'error|warning'` no verás
  nada, el paso parecerá limpio y `ctest` correrá el **binario anterior**. Construye
  un directorio por invocación y comprueba el código de salida (`set -e`), no la
  ausencia de la palabra "error".
- **Cuidado con los pipes:** `python ... | tail` hace que `$?` sea el de `tail`.
  Usa `set -o pipefail` o captura el código antes de la tubería.
- **Cuantiza RGB565 redondeando, nunca truncando.** En tonos oscuros truncar
  desplaza el tono: `0x263036` truncado daba verde==azul y las tarjetas se veían
  verdosas. Usa `UI_RGB565()` de `common/include/ui_theme.h`.
- **El brillo no toca el framebuffer.** Es la retroiluminación (PWM). Atenuar
  dentro del RGB565 recuantiza y vuelve a virar el color.
- **Objetivos táctiles ≥ 80 px.** El panel real tiene 235 ppi: 80 px = 8.65 mm.
  48 px son 5.2 mm, imposibles de acertar con el dedo. Hay un test que lo comprueba.
- **Dibujo y zona táctil van juntos.** Los widgets registran su `hit_add` en la
  misma llamada que los pinta. Un botón dibujado sin zona se ve pero no responde.
- **Glifos ausentes se dibujan como `?` en silencio.** Si añades texto con
  caracteres nuevos, agrégalos a `EXTRA` en `tools/ui/gen_font.py` y regenera el
  atlas; `tests/preview/test_font_coverage.py` te avisará.

## Mensajes de commit

En español, en imperativo, explicando **por qué** además de qué:

```
preview: arreglar HEAD/OPTIONS en 501 que tumbaba la vista previa

El handler solo implementaba GET y POST. Los proxies sondan con HEAD para
decidir si el upstream vive; con 501 marcaban el preview como caído aunque
GET / devolviera 200.
```

## Reportar un problema

Usa las plantillas de issue. Para un fallo del simulador o del preview, adjunta
una captura de `build_preview/shots/` y di qué esperabas ver: en la UI, una imagen
describe el bug mejor que un párrafo.

Para temas de seguridad, **no abras un issue público**: mira `SECURITY.md`.
