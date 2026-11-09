# Preview web del simulador C

Expone el simulador SDL2 real (`simulator/main.c`) en el navegador, sin necesidad
de servidor X. El puente reutiliza el código C tal cual: incluye `main.c` renombrando
su `main`, y llama a `render_screen()` y `handle_event()` directamente.

## Arrancar

Si ya hay SDL2 y CMake disponibles:

```bash
bash tools/preview/build.sh          # compila build_preview/bridge + smoke test
python3 tools/preview/server.py --port 8080
```

Si el entorno perdió las dependencias (típico tras un snapshot), un solo comando
las reconstruye y compila el puente:

```bash
bash tools/preview/bootstrap.sh      # CMake + Ninja + SDL2 + puente
python3 tools/preview/server.py --port 8080
```

`bootstrap.sh` es idempotente: reutiliza lo que ya exista. Verificado en frío
(sin nada cacheado y sin `cmake`/`ninja` en PATH) en **~59 s**, y el binario
resultante sirve fotogramas, navegación y animación correctamente.

Abrir el puerto 8080 en la vista previa. El servidor escucha en `0.0.0.0` y usa
rutas relativas, así que funciona detrás del proxy de la plataforma.

### Variables de entorno

| Variable | Efecto |
|---|---|
| `SDL2_PREFIX` | Prefijo de SDL2 a usar; tiene prioridad sobre pkg-config y PATH |
| `GIGA_BUILD_DIR` | Directorio de salida del puente (por defecto `build_preview/`) |
| `GIGA_DEPS_PREFIX` | Dónde vive/construir SDL2 (por defecto `~/.cache/giga-deps`) |
| `GIGA_TOOLS_PREFIX` | Dónde instalar CMake/Ninja (por defecto `~/.cache/giga-tools`) |

Las dos últimas permiten probar el bootstrap en un prefijo aislado sin pisar el
binario del preview en ejecución:

```bash
GIGA_DEPS_PREFIX=/tmp/cold/deps GIGA_TOOLS_PREFIX=/tmp/cold/tools \
  GIGA_BUILD_DIR=/tmp/cold/build bash tools/preview/bootstrap.sh
```

## Comprobar que está vivo

```bash
curl -s http://127.0.0.1:8080/health          # -> ok
ps -eo pid,args | grep -E '[t]ools/preview/server.py|[b]uild_preview/bridge'
```

Si el proceso murió pero el puerto sigue ocupado, identificar y matar el PID
antes de rearrancar. Si `build_preview/bridge` no existe (los directorios
`build_*/` no persisten entre snapshots), volver a correr `build.sh`.

## Por qué las fuentes viven aquí

Antes todo el arnés estaba en `build_preview/`, que `.gitignore` excluye con
`build_*/`. Eso hacía que el preview se perdiera en cada snapshot y hubiera que
reescribirlo a mano. Ahora:

- `tools/preview/bridge.c` — puente C, versionado.
- `tools/preview/server.py` — servidor HTTP, versionado.
- `tools/preview/build.sh` — build reproducible, versionado.
- `build_preview/bridge` — binario generado, ignorado (correcto).

`build.sh` busca SDL2 en este orden: `pkg-config`, `sdl2-config` en PATH, y por
último el prefijo local `~/.cache/giga-deps/install`. Añade el padre de
`includedir` porque `main.c` incluye `<SDL2/SDL.h>`, no `<SDL.h>`.

## Protocolo

El puente lee comandos por stdin y responde por stdout un fotograma crudo:

| Comando | Efecto |
|---|---|
| `F` | renderizar el estado actual |
| `K <keycode>` | inyectar tecla SDL (27=Escape, 13=Enter, 49–54=1–6) |
| `M <x> <y>` | toque en coordenadas de pantalla (0–799, 0–479) |
| `Q` | terminar |

Respuesta: cabecera `<III` (pantalla, batería, retroiluminación 0–255) +
`800*480*2` = **768000** bytes RGB565 (**768012** con cabecera).

El tercer campo es el **brillo como retroiluminación**, no como contenido del
panel: el servidor lo aplica al expandir a 8 bits por canal. Atenuar dentro del
RGB565 recuantizaba tonos oscuros (solo 32/64 niveles) y el gris viraba a verde.
El servidor HTTP traduce eso a `/frame.raw` y `/input`.

## Capturas de las 7 pantallas

```bash
bash tools/preview/build.sh
python3 tools/preview/shots.py            # -> build_preview/shots/*.png
python3 tools/preview/shots.py --screen 2 --tap 356 308   # con un toque
```

`shots.py` habla con el puente real y escribe PNG con `zlib`/`struct` (no necesita
PIL). **Sirve para mirar la interfaz, no solo para medir tests**: los seis defectos
de layout de esta pasada (transiciones verticales ausentes en CH2, hueco muerto en
Fuente, solapamiento del contador de página en WiFi, flecha atrás con aspecto
activo pero inerte, etiqueta encogida, hueco en Acerca de) pasaban el smoke test en
verde y solo se vieron renderizando a PNG.

## Reloj del puente

`handle_event()` fija plazos con `SDL_GetTicks()` (resaltado de pulsación de
110 ms, fin del escaneo WiFi). El puente debe usar **el mismo reloj** y expirar
esos estados; si mide el tiempo con `CLOCK_MONOTONIC` aparte, el botón pulsado se
queda resaltado para siempre. Por eso inicializa `SDL_INIT_TIMER` y usa
`SDL_GetTicks()`.

## Interfaz Material Design 3

El simulador usa **Roboto con antialiasing** (`common/include/roboto_font.h`, atlas de
4 bits generado por `tools/ui/gen_font.py`) y componentes Material: superficies
tonales, switches de pastilla, chips, slider y barra de navegación de 3 botones.
La capa de dibujo está en `common/ui_material.c`.

Roboto es de Google bajo SIL OFL 1.1; el texto de la licencia está en
`simulator/fonts/OFL-Roboto.txt`. Regenerar el atlas (solo si cambia la UI):

```bash
pip install pillow                      # en un venv
python3 tools/ui/gen_font.py --ttf Roboto-VF.ttf
```

El `.h` está versionado, así que compilar no necesita Pillow ni red. Si añades
texto con caracteres nuevos, `tests/preview/test_font_coverage.py` falla hasta que
los incluyas en `EXTRA` y regeneres: sin eso la UI muestra `?` en su lugar.

## Alcance

Renderiza el simulador tal como está hoy: **800×480**, el modo landscape del GIGA
Display Shield real (480×800 nativo, ST7701 por DSI, táctil GT911). Objetivos
táctiles de **80–96 px** = 8.6–10.4 mm en el panel real. Ver
`docs/HARDWARE_FIDELITY.md`. Este preview no mide rendimiento ni valida hardware:
el firmware no se compila en este entorno.
