# Gigaspark OS - Simulador Nativo (SDL2)

## Que es el Simulador

El simulador permite ejecutar apps de Gigaspark OS en tu PC sin necesidad del hardware fisico. Usa SDL2 para crear una ventana que simula la pantalla 480x272 del Giga Display Shield, y el raton funciona como pantalla tactil.

## Dependencias

### Linux (CachyOS/Arch)
```bash
sudo pacman -S sdl2 gcc make pkgconf
```

### Linux (Ubuntu/Debian)
```bash
sudo apt install libsdl2-dev gcc make pkg-config
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
cd simulator
mkdir build && cd build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/gigaspark_sim
```

### Prueba automatica sin ventana

```bash
SDL_VIDEODRIVER=dummy ./build/gigaspark_sim --smoke
# Con Make:
make test
```

Las pantallas estaticas solo se redibujan con entrada, cambio de reloj o bateria. El bucle duerme en lugar de renderizar 30 FPS continuamente. Las animaciones de osciloscopio y RF mantienen el limite de 30 FPS.

### Opcion 3: Compilar manualmente
```bash
cd simulator
gcc -std=c11 -Wall -Wextra -O2 \
    -I../sdk -I../common/include \
    $(pkg-config --cflags sdl2) \
    -o gigaspark_sim main.c sdk_stubs.c \
    $(pkg-config --libs sdl2) -lm
./gigaspark_sim
```

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
├── main.c           # SDL2: ventana, framebuffer, touch, launcher
├── sdk_stubs.c      # Implementaciones PC de las funciones del SDK
├── CMakeLists.txt   # Build system CMake
└── Makefile         # Build system simple
```

### Componentes

1. **Framebuffer** (`sim_fb`): Array 480x272 de uint16_t (RGB565), igual que el hardware real.
2. **SDK API Table**: Tabla de funciones en `sdk_stubs.c` que implementa `fill_rect`, `draw_text`, `get_touch`, etc. usando SDL2.
3. **Launcher**: Menu interactivo que muestra la lista de apps del sistema.
4. **Touch**: El raton de la PC simula el touchscreen (click = toque).

### Diferencias con el Hardware Real

| Caracteristica | Hardware (STM32) | Simulador (PC) |
|---------------|------------------|----------------|
| Pantalla | Fisica via LTDC | Ventana SDL2 |
| Touch | Resistivo/Capacitivo | Raton |
| CPU | Cortex-M7 480MHz | x86/ARM PC |
| Memoria | 512KB RAM | Ilimitada |
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
