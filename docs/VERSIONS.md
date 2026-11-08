# Versiones de la stack — noviembre de 2026

Todas las versiones de abajo se comprobaron contra la API de releases de GitHub
el 2026-11-07, no se copiaron de memoria.

| Tecnología | Antes | Ahora | Dónde se fija |
|---|---|---|---|
| Zephyr RTOS | 4.4.1 (solo en el README) | **4.5.0** | `west.yml` |
| Zephyr SDK | `gcc-arm-none-eabi` de apt | **1.0.1** (`arm-zephyr-eabi`) | `.github/workflows/build.yml` |
| Python | sin fijar | **3.13** (mínimo 3.12) | `.github/workflows/build.yml` |
| SDL | SDL2, commit suelto | **SDL3 3.4.20**, SDL2 **2.32.10** de respaldo | `simulator/sdl_compat.h`, `bootstrap.sh` |
| actions/checkout | v4 | **v7** | `.github/workflows/build.yml` |
| actions/upload-artifact | v4 | **v7** | `.github/workflows/build.yml` |
| actions/setup-python | no se usaba | **v7** | `.github/workflows/build.yml` |
| LVGL | "9.5.0" escrito a mano | la revisión del manifiesto de Zephyr | `west.yml` (import) |

## El CI de firmware nunca pudo funcionar

Faltaba `west.yml`. Los jobs ejecutaban `west init -l .` y `west update`, que
**requieren** un manifiesto: sin él fallan siempre. Además instalaban
`gcc-arm-none-eabi` de apt, que no es el toolchain que Zephyr espera (usa
`arm-zephyr-eabi` del SDK, con picolibc).

Ahora `west.yml` fija Zephyr `v4.5.0` e importa solo los módulos que este proyecto
usa. Los 8 nombres del `name-allowlist` se validaron contra el manifiesto real de
Zephyr 4.5.0 (81 proyectos): un nombre inexistente rompe `west update`.

```
cmsis  cmsis-dsp  hal_stm32  open-amp  libmetal  lvgl  mbedtls  picolibc
```

Importar el manifiesto completo descarga decenas de HALs de otros fabricantes
(>1 GB) sin aportar nada a un STM32H747.

**LVGL no se fija a mano.** Zephyr arrastra su propio pin
(`modules/lib/gui/lvgl`); sobrescribirlo rompe la integración de display. El
README decía "9.5.0" sin respaldo: ese pin no expone un `lv_version.h` legible, así
que se documenta como "la revisión del manifiesto" en vez de inventar un número.

**Esto no está verificado en hardware ni compilado aquí.** No hay workspace de
Zephyr ni toolchain ARM en este entorno. Lo que sí se comprobó: los nombres de
board existen en 4.5.0 (`board.yml`: `arduino_giga_r1` / `stm32h747xx`), los assets
del SDK 1.0.1 existen, `zephyr-env.sh` sigue presente y el YAML es válido. La
primida ejecución real del CI es la prueba que falta.

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

`common/include/ui_theme.h` es ahora la fuente única del color para el firmware
LVGL y el simulador. Antes cada uno tenía literales propios: el firmware una
paleta retro (`0x1a1a2e` / `0xe94560`) y el simulador otra, así que lo visto en el
PC no era lo que saldría en el Arduino.

Los tonos se definen en RGB888 y `UI_RGB565()` cuantiza **redondeando**. Truncar
desplaza el tono en superficies oscuras: `0x263036` truncado daba (33,48,49), con
verde==azul y las tarjetas se veían verdosas; redondeando da (41,48,57).

**Aviso honesto:** `m7/src/launcher.c` **no está en `target_sources`** del
`CMakeLists.txt` raíz y nadie llama a sus funciones: es código muerto. Alinear su
paleta lo deja coherente, pero **no cambia lo que dibuja el firmware hoy**.

## Lo que no se pudo actualizar

- **CMake**: 4.4.5 es lo último publicado, pero `cmake_minimum_required` se deja en
  3.20 a propósito. Subirlo excluiría a quien tenga el CMake de su distro sin
  ganar nada: el proyecto no usa ninguna feature posterior.
- **Ninja**: el runner ya trae una versión reciente; no se fija un pin que habría
  que mantener a mano.
