# Versiones de la stack — noviembre de 2026

Todas las versiones de abajo se comprobaron contra la API de releases de GitHub
el 2026-11-07, no se copiaron de memoria.

| Tecnología | Antes | Ahora | Dónde se fija |
|---|---|---|---|
| Zephyr RTOS | 4.4.1 (solo en el README) | **4.5.0** | `west.yml` |
| Zephyr SDK | `gcc-arm-none-eabi` de apt | **1.0.1** (`arm-zephyr-eabi`) | `.github/workflows/build.yml` |
| Python | sin fijar | **3.15.2** (workflow `3.15`, mínimo 3.12) | `.github/workflows/build.yml` |
| SDL | SDL2, commit suelto | **SDL3 3.4.20**, SDL2 **2.32.10** de respaldo | `simulator/sdl_compat.h`, `bootstrap.sh` |
| CMake | sin fijar | **4.4.5** (runner; mínimo del proyecto 3.20) | runner / `CMakeLists.txt` |
| Ninja | sin fijar | **1.13.2** (runner) | runner |
| actions/checkout | v4 | **v7** | `.github/workflows/build.yml` |
| actions/upload-artifact | v4 | **v7** | `.github/workflows/build.yml` |
| actions/setup-python | no se usaba | **v7** | `.github/workflows/build.yml` |
| LVGL | "9.5.0" escrito a mano | **eliminado** (render propio en RGB565) | `common/ui_os.c` |

## CI de firmware: estado actual

El fallo original era que faltaba `west.yml`; `west init -l .` lo requiere.
Ahora el manifiesto fija Zephyr `v4.5.0` y limita los imports a los ocho módulos
usados: `cmsis_6`, `cmsis`, `hal_stm32`, `open-amp`, `libmetal`, `fatfs`,
`mbedtls` y `picolibc`. El checker de lint valida los nombres contra los 81
proyectos del manifiesto upstream: `west` ignora en silencio un nombre erróneo.

El CI instala el SDK de Zephyr 1.0.1 (`arm-zephyr-eabi`), no el `gcc-arm-none-eabi`
de apt. También instala `pkg-config`, libusb y libudev porque `hidapi` no publica
wheel para Python 3.15 y se compila desde fuentes. El check del manifiesto usa
`GITHUB_TOKEN` para no topar con el rate limit anónimo de la API.

**Estado actual:** PR #1, run `42721414186`, 8/8 jobs SUCCESS: lint, host tests,
native_sim, M4, M7, SDL2, SDL3 y simulador. El job M7 publica `zephyr.bin` y
`zephyr.elf`; el ZIP de artefactos completo pesa 2 818 374 bytes (la ejecución
de referencia anterior pesaba 2 496 045; el ZIP no es el tamaño del binario).

**LVGL ya no se usa ni se importa.** Su único consumidor era `m7/src/ui_manager.c`,
código muerto que no se compilaba y mezclaba API de v8 y v9. Se eliminó junto con
el módulo del manifiesto. El firmware y el simulador usan el mismo render propio
RGB565 (`common/ui_os.c` + `common/ui_material.c`); el backend de panel es
`m7/src/display_giga.c`. No queda ningún pin de LVGL que mantener.

## SDL3 con respaldo en SDL2

SDL3 es la versión actual, pero **rompe la API** en casi todo lo que usa el
simulador. Debian/Ubuntu LTS todavía empaquetan SDL2, así que soportar solo SDL3
dejaría el proyecto sin compilar ahí. `simulator/sdl_compat.h` expone una API
única `gsp_*`:

| Diferencia | SDL2 | SDL3 |
|---|---|---|
| Éxito de `SDL_Init` | `int == 0` | `bool == true` |
| Crear ventana | con `x, y` | sin `x, y` (centra sola) |
| Crear renderer | índice + flags | nombre (`"software"`) |
| Copiar textura | `SDL_RenderCopy` | `SDL_RenderTexture` |
| Eventos | `SDL_QUIT`, `SDL_KEYDOWN` | `SDL_EVENT_QUIT`, `SDL_EVENT_KEY_DOWN` |
| Eventos de ventana | un `SDL_WINDOWEVENT` agrupado | un evento por caso |
| Tecla Q | `SDLK_q` | `SDLK_Q` |
| Keycode del evento | `ev->key.keysym.sym` | `ev->key.key` |
| Reloj | `Uint32` | `Uint64` |
| `SDL_INIT_TIMER` | existe | **eliminado** (el reloj no necesita init) |
| `SDL_SetMainReady` | vía `SDL.h` | requiere `SDL3/SDL_main.h` |
| Driver de video | `SDL_VIDEODRIVER` | `SDL_VIDEO_DRIVER` |

Cada diferencia de la tabla se confirmó en las cabeceras de 3.4.20 instaladas, no
se supuso. Las dos últimas producían fallos reales: `SDL_INIT_TIMER` no compila y
`SDL_SetMainReady` quedaba como declaración implícita (warning aquí, error con
`-Werror` en CI).

La detección vive en un único sitio, `cmake/GigaSDL.cmake`, para que el simulador
y los tests no diverjan. El Makefile y `tools/preview/build.sh` hacen su propia
detección porque no usan CMake.

```bash
cmake -S simulator -B build -DGIGA_FORCE_SDL2=ON   # probar la ruta SDL2
make -C simulator SDL=2                            # idem con make
make -C simulator SDL_PREFIX=/ruta                 # SDL sin pkg-config
```

**Verificado, no supuesto:** las 7 pantallas producen el framebuffer
**idéntico byte a byte** con SDL2 y con SDL3 (hash SHA-256 del frame completo
`b9687367daf73d1d`), y los 4 tests de host más el smoke pasan en ambas rutas. Un
job `sdl-matrix` de CI comprueba las dos para que el respaldo no se rompa en
silencio.

SDL3 se construye con `-DSDL_UNIX_CONSOLE_BUILD=ON`: aborta la configuración si no
encuentra X11 ni Wayland, y el preview es headless (renderiza a un framebuffer y lo
sirve por HTTP).

## Tema visual compartido

`common/include/ui_theme.h` es la fuente única del color para el firmware y el
simulador (ambos usan el mismo núcleo `common/ui_os.c`). La extracción del núcleo
eliminó la posibilidad de que la paleta del panel y la de escritorio diverjan.

Los tonos se definen en RGB888 y `UI_RGB565()` cuantiza **redondeando**. Truncar
desplaza el tono en superficies oscuras: `0x263036` truncado daba (33,48,49), con
verde==azul y las tarjetas se veían verdosas; redondeando da (41,48,57).

**Alcance:** el antiguo `m7/src/launcher.c` y `ui_manager.c` se eliminaron: no
estaban en `target_sources` y no tenían consumidores. El panel real ahora usa
`m7/src/display_giga.c` y el núcleo compartido `common/ui_os.c`.

## Lo que no se pudo actualizar

- **CMake**: 4.4.5 es lo último publicado, pero `cmake_minimum_required` se deja en
  3.20 a propósito. Subirlo excluiría a quien tenga el CMake de su distro sin
  ganar nada: el proyecto no usa ninguna feature posterior.
- **Ninja**: el runner ya trae una versión reciente; no se fija un pin que habría
  que mantener a mano.
