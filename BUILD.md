# 🔨 BUILD.md — Guia de Compilacion

## Requisitos

### Sistema Operativo
- **Linux:** CachyOS, Arch, Ubuntu 22.04+, Fedora
- **macOS:** 12+ (Monterey o posterior)
- **Windows:** WSL2 con Ubuntu

### Herramientas Necesarias

| Herramienta | Version Minima | Instalacion |
|-------------|----------------|-------------|
| Git | 2.x | `sudo pacman -S git` |
| CMake | 3.20 | `sudo pacman -S cmake` |
| Python | 3.12+ (CI: 3.15) | `sudo pacman -S python` |
| Ninja | 1.10 | `sudo pacman -S ninja` |
| dtc | 1.5+ | `sudo pacman -S dtc` |
| Zephyr SDK | 1.0.1 | Ver abajo |

### Instalar Zephyr SDK 1.0.1

```bash
# Sigue el mismo paquete minimal + toolchain ARM que usa CI.
SDK=1.0.1
BASE="https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v$SDK"
wget -q "$BASE/zephyr-sdk-${SDK}_linux-x86_64_minimal.tar.xz"
tar xf "zephyr-sdk-${SDK}_linux-x86_64_minimal.tar.xz" -C "$HOME"
wget -q -O toolchain.tar.xz \
  "$BASE/toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz"
tar xf toolchain.tar.xz -C "$HOME/zephyr-sdk-${SDK}"
"$HOME/zephyr-sdk-${SDK}/setup.sh" -t arm-zephyr-eabi -c
```

### Configurar el workspace de Zephyr

Ejecuta desde la raíz de este repositorio. `west.yml` es el manifiesto local;
`west init -l .` crea el workspace en el directorio padre del repo.

```bash
# Instalar host tools (Debian/Ubuntu; incluye dependencias nativas de hidapi
# para que funcionen los requirements de Zephyr con Python 3.15).
sudo apt-get install -y git cmake ninja-build gperf ccache \
  dfu-util device-tree-compiler pkg-config libusb-1.0-0-dev libudev-dev

python3 -m venv ~/.venvs/zephyr
source ~/.venvs/zephyr/bin/activate
python -m pip install --upgrade pip west
west init -l .
west update --narrow -o=--depth=1
ZEPHYR="$(west topdir)/zephyr"
pip install -r "$ZEPHYR/scripts/requirements.txt"
west zephyr-export

export ZEPHYR_BASE="$ZEPHYR"
export ZEPHYR_SDK_INSTALL_DIR="$HOME/zephyr-sdk-1.0.1"
```

El CI usa Python 3.15; Zephyr 4.5.0 requiere Python >=3.12. Con Python 3.15
`hidapi` se compila desde fuente, por eso hacen falta `pkg-config`, libusb y
libudev.

---

## Compilar

### Firmware M7 + backend de pantalla

Desde la raíz del repositorio, con el entorno anterior activado:

```bash
west build -p -b arduino_giga_r1/stm32h747xx/m7 \
  --shield arduino_giga_display_shield
```

Este es el comando verificado por el job `Build M7 Firmware`. La salida incluye
`build/zephyr/zephyr.bin` y `build/zephyr/zephyr.elf`. El overlay de la raíz
habilita el LTDC, el panel ST7701 y el GT911.

### Firmware M4 (opcional)

```bash
west build -p -b arduino_giga_r1/stm32h747xx/m4 m4
```

Este es el comando del job `Build M4 Firmware`. Las cifras de flash/RAM dependen
de la configuración y del SDK; no se fija aquí una salida antigua como garantía.
Los artefactos `.bin`/`.elf` más recientes los publica GitHub Actions.

**Validación física pendiente:** una compilación correcta no demuestra que el
panel encienda. Timings, orientación táctil y brillo SWIRE necesitan el Arduino
GIGA Display Shield conectado; ver `docs/HARDWARE_FIDELITY.md`.

---

## Flashear

### Por USB (DFU)

```bash
# Poner el Giga R1 en modo DFU
# 1. Mantener presionado el boton BOOT
# 2. Presionar y soltar RESET
# 3. Soltar BOOT

# Flashear
west flash
```

### Por ST-Link (SWD)

```bash
west flash --runner stlink
```

### Verificar

```bash
# Abrir monitor serial
minicom -D /dev/ttyACM0 -b 115200

# O usar picocom
picocom -b 115200 /dev/ttyACM0
```

---

## Apps Standalone

Las apps de `test_apps/` se compilan como parte del firmware principal. Para agregar una nueva app:

1. Crear directorio en `test_apps/mi_app/`
2. Crear `main.c` con funcion `app_main(void)`
3. Agregar a `CMakeLists.txt`:
   ```cmake
   target_sources(app PRIVATE test_apps/mi_app/main.c)
   ```
4. Recompilar

### Apps Incluidas

| App | Directorio | Descripcion |
|-----|------------|-------------|
| Clicker | `test_apps/clicker/` | Demo de touch basico |
| Osciloscopio | `test_apps/oscilloscope_app/` | Captura waveform CH1 |
| Fuente Lab | `test_apps/power_supply_app/` | Control voltaje/corriente |
| Flipper RF | `test_apps/flipper_app/` | NFC, IR, 433MHz |

---

## Solucion de Problemas

### Error: `libprotobuf.so.36.1.0` not found
```bash
# Actualizar protobuf
sudo pacman -S protobuf python-protobuf
```

### Error: ` west: command not found`
```bash
source ~/.venvs/zephyr/bin/activate
```

### Error: `ZEPHYR_BASE not set`
```bash
export ZEPHYR_BASE=~/zephyr-ws/zephyr
export ZEPHYR_SDK_INSTALL_DIR=~/zephyr-sdk-1.0.1
```

### Error: `adc_channel_setup_dt` falla
Los drivers de hardware funcionan en modo stub si los periféricos no estan habilitados en el Device Tree. Ver `m7/src/hw/*.c` para instrucciones de habilitacion.

---

## Tamaño del Firmware

No se mantienen las cifras históricas de flash/RAM que aparecían aquí: no
coincidían con el mapa de memoria real del H747 y podían confundirse con el
framebuffer. La UI reserva **768 000 bytes en SDRAM externa** (800×480 RGB565);
los `.bin` y `.elf` actuales se descargan como artefactos de los jobs M7/M4 en
GitHub Actions. La compilación CI no sustituye la validación de encendido del
panel en la placa física.
