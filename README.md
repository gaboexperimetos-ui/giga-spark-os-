# Gigaspark OS — The Maker's Mega-Device

> **Un sistema operativo dual-core que convierte al Arduino Giga R1 en una navaja suiza de la ingeniería.**

![STM32H747](https://img.shields.io/badge/STM32H747-Dual--Core-blue)
![Zephyr RTOS](https://img.shields.io/badge/Zephyr-RTOS-4.4.1-green)
![C99](https://img.shields.io/badge/Language-C99-blue)
![License](https://img.shields.io/badge/License-MIT-yellow)
![Version](https://img.shields.io/badge/Version-1.0-red)
![Build](https://img.shields.io/badge/Build-Passing-brightgreen)

---

## ✨ Descripcion

**Gigaspark OS** es un sistema operativo de tiempo real (RTOS) dual-core para el microcontrolador **STM32H747XI** (Arduino Giga R1). Convierte una placa de desarrollo en una herramienta profesional de laboratorio con:

- 🔬 **Osciloscopio** de 2 canales (0-30V, 1 MSPS)
- ⚡ **Fuente de alimentacion** dual programable (0-30V, 0-5A)
- 🏷️ **Lector NFC** (Mifare/NTAG)
- 📡 **Transceptor Sub-1GHz** (433MHz, estilo Flipper Zero)
- 📺 **Control IR** (NEC, RC5, Sony)
- 🖥️ **Interfaz grafica** táctil con barra de navegación
- 🌐 **WiFi** para descarga de apps
- 🎵 **Audio I2S** y **Video JPEG** por hardware
- 💾 **Sistema de archivos** FAT32 en MicroSD

---

## 📋 Tabla de Caracteristicas

| Fase | Caracteristica | Estado |
|------|----------------|--------|
| Fase 1 | Dual-Core IPC (M7 ↔ M4 via OpenAMP/RPMsg) | ✅ |
| Fase 2 | Motor de memoria con handles | ✅ |
| Fase 3 | Compresion zRAM (RLE) automatica | ✅ |
| Fase 4 | Memoria 3-tier (RAM → zRAM → SD swap) | ✅ |
| Fase 5+6 | Sistema de archivos FAT32 + App Launcher | ✅ |
| Fase 9 | Fault recovery + SDK export table | ✅ |
| Fase 10 | WiFi App Downloader + File write IPC | ✅ |
| Fase 11 | Power Manager (Sleep por inactividad 15s) | ✅ |
| Fase 12 | Nav bar Android + Cache cleanup L1 | ✅ |
| Fase 13 | Motor Multimedia (JPEG HW + Audio I2S) | ✅ |
| Fase 14 | Drivers Mega-Dispositivo (HAL completo) | ✅ |
| Fase 15 | Apps de Sistema (Oscilloscope, Lab PSU, Flipper) | ✅ |
| Hardware | Esquematico KiCad 4 bloques | ✅ |

---

## 🏗️ Arquitectura del Sistema

```
┌─────────────────────────────────────────────────────┐
│                  GIGASPARK OS                       │
├─────────────────────┬───────────────────────────────┤
│   CORTEX-M7 (480MHz)│   CORTEX-M4 (240MHz)         │
│   ★ Nucleo Primario │   ★ Nucleo Remoto            │
├─────────────────────┼───────────────────────────────┤
│ Thread 1: ui_launcher│ Thread 1: ipc_listener       │
│   (Prio 14, UI)     │   (Prio 5, IPC receive)      │
│ Thread 2: app_runner │ Thread 2: mem_engine          │
│   (Prio 5, Apps)    │   (Prio 7, Memoria+FS)       │
├─────────────────────┼───────────────────────────────┤
│ • LVGL GUI (9.x)    │ • 3-tier Memory Engine        │
│ • Fault Recovery     │ • zRAM Compression (RLE)      │
│ • Power Manager      │ • SD Swap (64 sectors)        │
│ • Nav Bar (40px)     │ • FAT32 File System           │
│ • Video Decoder      │ • Media File Read/Write       │
│ • Audio I2S          │ • App Binary Loader           │
│ • WiFi (stub)        │                               │
├─────────────────────┼───────────────────────────────┤
│ HAL Drivers:         │                               │
│ • Oscilloscope (ADC) │                               │
│ • Power Supply (SPI) │                               │
│ • RF Tools (CC1101,  │                               │
│   PN532, IR TX/RX)   │                               │
└─────────────────────┴───────────────────────────────┘
         │ IPC (OpenAMP/RPMsg over SRAM4)
         ▼
┌─────────────────────────────────────────────────────┐
│              HARDWARE (KiCad Design)                │
├──────────┬──────────┬──────────┬────────────────────┤
│ Bloque 1 │ Bloque 2 │ Bloque 3 │ Bloque 4          │
│ MCU+IO   │ Analog   │ PSU Dual │ RF+Controls       │
│ SD+Audio │ Osc+DMM  │ 30V/5A   │ CC1101+PN532+IR   │
└──────────┴──────────┴──────────┴────────────────────┘
```

---

## Estructura de Directorios

```
gigaspark_os/
├── .github/workflows/build.yml   # CI/CD pipeline
├── CMakeLists.txt                 # Build principal (M7)
├── prj.conf                       # Configuracion M7
├── app.overlay                    # Device Tree overlay M7
│
├── common/                        # Codigo compartido M4/M7
│   ├── include/
│   │   ├── ipc_protocol.h         # Comandos IPC
│   │   └── ...
│   └── ipc_config.h               # Endpoint IPC
│
├── m4/                            # Firmware nucleo M4
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── src/
│   │   ├── main.c                 # M4 main (2 threads)
│   │   ├── mem_engine.c/h         # 3-tier memory + Buddy
│   │   ├── zram_comp.c/h          # RLE + LRU eviction
│   │   ├── sd_swap.c/h            # SD swap simulation
│   │   └── fs_manager.c/h         # FAT32 + Media read
│   └── Kconfig
│
├── m7/                            # Firmware nucleo M7
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── src/
│   │   ├── main.c                 # M7 main (2 threads)
│   │   ├── app_runner.c/h         # App execution + fault recovery
│   │   ├── fault_manager.c/h      # PendSV + setjmp/longjmp
│   │   ├── nav_bar.c/h            # Android-style nav bar
│   │   ├── power_manager.c/h      # Sleep mode (configurable)
│   │   ├── video_decoder.c/h      # JPEG HW decoder
│   │   ├── audio_i2s.c/h          # I2S audio playback
│   │   ├── net_manager.c/h        # WiFi (stub)
│   │   ├── app_downloader.c/h     # HTTP download (stub)
│   │   └── hw/                    # Drivers Mega-Dispositivo
│   │       ├── oscilloscope.c/h
│   │       ├── power_supply.c/h
│   │       └── rf_tools.c/h
│   └── Kconfig
│
├── sdk/                           # API exportada a apps
│   ├── gigaspark_api.h
│   └── gigaspark_api.c
│
├── simulator/                     # Simulador nativo SDL2
│   ├── main.c
│   ├── sdk_stubs.c
│   ├── CMakeLists.txt
│   └── Makefile
│
├── test_apps/                     # Apps de usuario
│   ├── clicker/main.c
│   ├── oscilloscope_app/main.c
│   ├── power_supply_app/main.c
│   ├── settings_app/main.c
│   └── flipper_app/main.c
│
├── tests/unit/                    # Tests unitarios (ZTEST)
│   ├── mem_engine/
│   │   └── test_buddy_allocator.c
│   └── zram_comp/
│       └── test_zram_lru.c
│
├── hardware/                      # Diseno KiCad
│   └── Gigaspark_Mega/
│       ├── Gigaspark_Mega.kicad_pro
│       ├── Gigaspark_Mega.kicad_sch
│       ├── Gigaspark_Mega.kicad_pcb
│       └── docs/
│           ├── BLOCK1_DESIGN.md
│           ├── BLOCK2_DESIGN.md
│           ├── BLOCK3_DESIGN.md
│           ├── BLOCK4_DESIGN.md
│           └── PCB_LAYOUT_GUIDE.md
│
├── docs/
│   ├── Doxyfile                   # Generacion Doxygen
│   ├── SIMULATOR.md               # Guia del simulador
│   └── API.md
│
├── BUILD.md                       # Guia de compilacion
├── HARDWARE.md                    # Guia de ensamblaje
├── LICENSE                        # MIT License
└── README.md                      # Este archivo
```

---

## Quick Start (Simulador)

Ejecuta Gigaspark OS en tu PC sin hardware fisico:

```bash
# Instalar dependencias
sudo pacman -S sdl2 gcc make pkgconf    # Arch/CachyOS
sudo apt install libsdl2-dev gcc make pkg-config  # Ubuntu/Debian

# Compilar y ejecutar
cd simulator
make
./gigaspark_sim
```

La ventana se abre con el launcher. Usa el mouse como touch, flechas para navegar, Enter para seleccionar, ESC para volver.

Ver [docs/SIMULATOR.md](docs/SIMULATOR.md) para instrucciones detalladas.

Para ejecutar la prueba automatica sin ventana: `SDL_VIDEODRIVER=dummy ./gigaspark_sim --smoke`.
Las optimizaciones, mediciones y limites de verificacion estan en [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

---

## Inicio Rapido (Hardware)

```bash
# Clonar el repositorio
git clone https://github.com/gabo/gigaspark_os.git
cd gigaspark_os

# Compilar firmware M7 (requiere Zephyr SDK)
west build -p -b arduino_giga_r1/stm32h747xx/m7 --sysbuild .

# Flashear por USB
west flash
```

Ver [BUILD.md](BUILD.md) para instrucciones detalladas.

---

## 📐 Hardware

El diseño del PCB esta en `hardware/Gigaspark_Mega/`. Ver [HARDWARE.md](HARDWARE.md) para la lista de materiales y guia de ensamblaje.

---

## Metricas del Sistema

| Metrica | Valor |
|---------|-------|
| Flash M7 | 143 KB / 768 KB (18%) |
| RAM M7 | 125 KB / 512 KB (24%) |
| Flash M4 | 61 KB |
| RAM M4 | 40 KB (31%) |
| Tests | 4 suites de host + smoke + 9 de preview (ver `docs/VERSIONS.md`) |
| Commits | 23 |
| Phases | 22 completadas |

---

## Tecnologias

- **RTOS:** Zephyr 4.5.0 (fijado en `west.yml`)
- **Simulador:** SDL3 3.4.20, con respaldo en SDL2 2.32.10
- **Versiones y su verificación:** [`docs/VERSIONS.md`](docs/VERSIONS.md)
- **MCU:** STM32H747XI (Cortex-M7 480MHz + M4 240MHz)
- **GUI:** LVGL — la revisión que arrastra el manifiesto de Zephyr 4.5.0
- **IPC:** OpenAMP/RPMsg
- **Simulador:** SDL2 nativo (sin dependencias Zephyr)
- **Hardware Design:** KiCad 10.0.6
- **Lenguaje:** C puro (C99/C11)
- **CI/CD:** GitHub Actions

---

## 📜 Licencia

MIT License - Ver [LICENSE](LICENSE) para detalles.

---

## 👤 Autor

**Gabo** — [gruposalas2025@gmail.com](mailto:gruposalas2025@gmail.com)

> *"Construido desde cero, sin hardware fisico, usando pura arquitectura e ingenieria de software."*
