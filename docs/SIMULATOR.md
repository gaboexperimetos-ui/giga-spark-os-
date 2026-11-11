# Gigaspark OS — Simulador nativo (SDL3 / SDL2)

## Que es el Simulador

El simulador permite ejecutar apps de Gigaspark OS en tu PC sin necesidad del hardware fisico. Usa SDL3 (con compatibilidad SDL2) para crear una ventana de **800x480** (escala 1:1) y el raton funciona como pantalla tactil.

> **Geometria:** el lienzo es **800x480**, el modo landscape del Giga Display Shield real (ASX00039): panel de 3.97" con **480x800** nativo, controlador **ST7701** por MIPI DSI y tactil **Goodix GT911**. Antes era 480x272, una resolucion que no corresponde a ningun panel de este shield; ya esta migrada. Fuentes y calculos en [HARDWARE_FIDELITY.md](HARDWARE_FIDELITY.md).

### Aspecto Material Design 3

La interfaz imita Android, no una terminal: **Roboto con antialiasing** (atlas de
cobertura de 4 bits en `common/include/roboto_font.h`), superficies tonales elevadas,
switches de pastilla, chips, slider y **barra de navegacion de 3 botones**
(atras / inicio / recientes) abajo, con barra de estado arriba y app bar.

El núcleo de interfaz está en `common/ui_os.c` y es portable: lo usan el
simulador y el firmware del M7. La capa de dibujo está en `common/ui_material.c`: mezcla con alfa en RGB565,
rectangulos redondeados y circulos antialiasados, iconos de trazo grueso y
widgets M3. Roboto es de Google, licencia SIL OFL 1.1
(`simulator/fonts/OFL-Roboto.txt`); el atlas se regenera con
`tools/ui/gen_font.py`.

**El brillo es retroiluminación, no contenido.** En el panel real la señal de
retroiluminación es SWIRE (no PWM): el simulador modela la luz emitida,
no altera los valores RGB565. El protocolo SWIRE aún no se implementa en firmware.
Atenuar dentro del framebuffer obligaba a recuantizar tonos ya oscuros y el gris
viraba a verde (una tarjeta pasaba de (33,36,41) a (24,32,33), con verde==azul).
Ahora `render_screen()` deja el framebuffer intacto a cualquier brillo y
`backlight_scale()` entrega el factor para que cada backend modele la luz por
separado. El hardware usa SWIRE en PB12; el protocolo físico de brillo queda
pendiente.

### Diseno tactil

El panel mide 86.47 x 51.88 mm utiles, o sea **0.1081 mm/pixel**. Eso convierte el
tamano de los botones en una cuestion fisica, no estetica:

| Lado menor | mm reales | Veredicto |
|---|---:|---|
| 36 px | 3.9 mm | inviable con el dedo |
| 48 px | 5.2 mm | insuficiente |
| **80 px** | **8.6 mm** | minimo de este simulador (`TOUCH_MIN`) |
| 96 px | 10.4 mm | comodo |

Todo objetivo tactil mide **>= 80 px** en su lado menor, y un test lo comprueba en
las 7 pantallas (`tests/host/test_sim_ui.c`). El layout es el cromo de Android:
36 px de barra de estado, 80 px de app bar, 284 px de contenido y 80 px de barra
de navegación inferior (ATRÁS / HOME / RECIENTES). 36 + 80 + 284 + 80 = 480.

**Dibujo y toque no pueden separarse:** `btn()` dibuja el boton y registra su zona
tactil en la misma llamada (`hit_add`). Antes se dibujaban botones sin zona, asi
que la interfaz se veia bien pero no respondia. Un boton desactivado se dibuja
apagado (`BS_OFF`) y **no** registra zona: no hay flechas que parezcan activas y no
hagan nada.

## Dependencias

### Linux (CachyOS/Arch)
```bash
sudo pacman -S sdl3 gcc make pkgconf
# SDL2 también está soportado: sudo pacman -S sdl2
```

### Linux (Ubuntu/Debian)
```bash
sudo apt install libsdl3-dev gcc make pkg-config
# Si la distro no empaqueta SDL3: libsdl2-dev (compatibilidad mantenida)
```

### macOS
```bash
brew install sdl2
```

### Windows
Instalar MSYS2 y luego:
```bash
pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-gcc make
```

## Compilar y Ejecutar

### Opcion 1: Make (recomendado)
```bash
cd simulator
make
./gigaspark_sim
```

### Opcion 2: CMake
```bash
cmake -S simulator -B build_simulator -DCMAKE_BUILD_TYPE=Release
cmake --build build_simulator
./build_simulator/gigaspark_sim
```

### Prueba automatica sin ventana

```bash
SDL_VIDEODRIVER=dummy SDL_VIDEO_DRIVER=dummy ./build_simulator/gigaspark_sim --smoke
# Con Make (desde simulator/):
make test
```

Las pantallas estaticas solo se redibujan con entrada, cambio de reloj o bateria. El bucle duerme en lugar de renderizar 30 FPS continuamente. Las animaciones de osciloscopio y RF mantienen el limite de 30 FPS.

### SDL2 explícito

Si no tienes SDL3, `make` detecta SDL2 automáticamente. También puedes forzarlo:

```bash
cd simulator
make clean
make SDL=2
./gigaspark_sim
```

Para una instalación fuera de las rutas del sistema, usa `SDL_PREFIX=/ruta`.
El Makefile y `tools/preview/build.sh` son la ruta recomendada; así se conserva
la selección correcta de SDL2/SDL3 y sus flags de enlace.

## Controles

| Tecla/Accion | Funcion |
|-------------|---------|
| Mouse click | Touch en la pantalla |
| Flecha Arriba | Navegar menu hacia arriba |
| Flecha Abajo | Navegar menu hacia abajo |
| Enter | Seleccionar app |
| Escape | Volver al launcher |
| q | Salir del simulador |

## Arquitectura

```
simulator/
├── main.c           # Backend SDL: ventana, eventos y reloj
├── sdl_compat.h     # Compatibilidad SDL2/SDL3
├── CMakeLists.txt   # Build system CMake
└── Makefile         # Build system simple
common/
├── ui_os.c          # Núcleo UI portable: render, hit-test, acciones
├── ui_material.c    # Primitivas y widgets Material
└── include/         # Tema, framebuffer y atlas Roboto
```

### Componentes

1. **Framebuffer** (`fb`): Array 800x480 de uint16_t (RGB565), 768000 bytes. Tanto el formato como la resolucion coinciden con el panel real en landscape.
2. **Núcleo portable**: `common/ui_os.c` renderiza y registra zonas táctiles; lo comparte el firmware.
3. **Widgets Material**: `common/ui_material.c`, Roboto y tema RGB565.
4. **Touch**: el ratón de la PC simula el panel (click = toque); en hardware, GT911.

### Diferencias con el Hardware Real

| Caracteristica | Hardware (STM32) | Simulador (PC) |
|---------------|------------------|----------------|
| Pantalla | 480x800 nativo, ST7701 por DSI vía LTDC | Ventana SDL 800x480 (landscape, 1:1) |
| Touch | Capacitivo Goodix GT911, 5 puntos | Raton (1 punto) |
| CPU | Cortex-M7 480MHz | x86/ARM PC |
| Memoria | 1 MB RAM + 8 MB SDRAM externa | Ilimitada |
| M4 Core | Fisico | No simulado |
| IPC | OpenAMP real | Stubs |
| Audio | I2S + DMA | No simulado |
| WiFi | CYW43439 | No simulado |

### Para Desarrolladores de Apps

El simulador usa **exactamente las mismas funciones del SDK** que el hardware real. Si tu app funciona en el simulador, funcionara en el Giga R1.

Para crear una nueva app:

1. Crea un archivo en `test_apps/tu_app/main.c`
2. Usa las macros del SDK: `giga_fill_rect()`, `giga_draw_text()`, `giga_get_touch()`, etc.
3. Implementa `void app_main(void)`
4. Compila con el simulador para probar en PC

## Solucion de Problemas

### "SDL2 no encontrado"
```bash
# Verificar que SDL2 esta instalado
pkg-config --libs sdl2

# En Arch/CachyOS:
sudo pacman -S sdl2

# En Ubuntu:
sudo apt install libsdl2-dev
```

### "Error de compilacion: undefined reference"
Asegurate de tener pkg-config instalado y SDL2 detectado correctamente.

### Ventana muy pequena/grande
Edita `SCALE` en `main.c (default: 2x = 960x544 pixeles).
