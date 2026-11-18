#!/bin/bash
# ============================================================
# Gigaspark OS - Flasher para Arduino Giga R1
# Flashea firmware M7 + M4 via USB DFU o OpenOCD
#
# Uso: ./flash.sh [opcion]
#   ./flash.sh          - Flashear M7 via DFU (recomendado)
#   ./flash.sh m7       - Flashear solo M7
#   ./flash.sh m4       - Flashear solo M4
#   ./flash.sh both     - Flashear M7 + M4
#   ./flash.sh dfu      - Forzar modo DFU
#   ./flash.sh monitor  - Abrir monitor serial
#   ./flash.sh clean    - Limpiar build
#   ./flash.sh build    - Compilar sin flashear
#   ./flash.sh help     - Mostrar ayuda
# ============================================================

set -e

# Colores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
BUILD_M4_DIR="${PROJECT_DIR}/build_m4"
FIRMWARE_M7="${BUILD_DIR}/zephyr/zephyr.bin"
FIRMWARE_M4="${BUILD_M4_DIR}/zephyr/zephyr.bin"

print_banner() {
    echo -e "${CYAN}"
    echo "  ╔══════════════════════════════════════╗"
    echo "  ║     GIGASPARK OS - FLASHER v1.0      ║"
    echo "  ║     Arduino Giga R1 (STM32H747)      ║"
    echo "  ╚══════════════════════════════════════╝"
    echo -e "${NC}"
}

check_deps() {
    local missing=0

    if ! command -v dfu-util &>/dev/null; then
        echo -e "${YELLOW}[WARN] dfu-util no instalado${NC}"
        echo "  Instalar: sudo pacman -S dfu-util  (Arch)"
        echo "            sudo apt install dfu-util (Ubuntu)"
        missing=1
    fi

    if ! command -v west &>/dev/null; then
        echo -e "${YELLOW}[WARN] west no encontrado${NC}"
        echo "  Instalar: pip3 install west"
        missing=1
    fi

    if ! command -v openocd &>/dev/null; then
        echo -e "${YELLOW}[WARN] openocd no instalado (opcional)${NC}"
        echo "  Instalar: sudo pacman -S openocd  (Arch)"
        echo "            sudo apt install openocd (Ubuntu)"
    fi

    if [ $missing -eq 1 ]; then
        echo ""
    fi
}

check_dfu() {
    if ! lsusb 2>/dev/null | grep -q "2341:0364\|2341:0365\|0483:df11"; then
        echo -e "${RED}[ERROR] Arduino Giga R1 no detectado en modo DFU${NC}"
        echo ""
        echo "  Para entrar en modo DFU:"
        echo "  1. Conectar el Giga R1 por USB"
        echo "  2. Presionar y mantener el boton BOOT"
        echo "  3. Presionar y soltar RESET"
        echo "  4. Soltar BOOT"
        echo ""
        echo "  O ejecutar: ./flash.sh dfu"
        return 1
    fi
    return 0
}

enter_dfu() {
    echo -e "${YELLOW}[*] Buscando Arduino Giga R1...${NC}"

    # Buscar por USB serial
    local port=""
    for p in /dev/ttyACM* /dev/ttyUSB*; do
        if [ -e "$p" ]; then
            port="$p"
            break
        fi
    done

    if [ -z "$port" ]; then
        echo -e "${YELLOW}[*] No se encontro puerto serial. Intentando DFU directo...${NC}"
    else
        echo -e "${GREEN}[+] Puerto encontrado: ${port}${NC}"
        echo -e "${YELLOW}[*] Enviando comando DFU...${NC}"
        # Enviar comando DFU via serial (Arduino sketch normalmente lo hace)
        echo -e "DFU" > "$port" 2>/dev/null || true
        sleep 1
    fi

    echo -e "${CYAN}[*] Instrucciones para modo DFU:${NC}"
    echo "  1. Desconectar el Giga R1"
    echo "  2. Presionar y mantener BOOT"
    echo "  3. Conectar por USB (o presionar RESET)"
    echo "  4. Soltar BOOT"
    echo ""
    echo "  El LED naranja debe parpadear = modo DFU activo"
}

build_firmware() {
    echo -e "${CYAN}[*] Compilando firmware...${NC}"

    # shellcheck source=/dev/null
    source ~/.venvs/zephyr/bin/activate 2>/dev/null || true
    export ZEPHYR_BASE=~/zephyr-ws/zephyr

    echo -e "${YELLOW}[1/2] Compilando M7 (Cortex-M7 480MHz)...${NC}"
    cd "$PROJECT_DIR"
    west build -b arduino_giga_r1/stm32h747xx/m7

    echo -e "${YELLOW}[2/2] Compilando M4 (Cortex-M4 240MHz)...${NC}"
    west build -b arduino_giga_r1/stm32h747xx/m4 --build-dir build_m4

    echo -e "${GREEN}[OK] Firmware compilado${NC}"
    echo "  M7: ${FIRMWARE_M7}"
    echo "  M4: ${FIRMWARE_M4}"
}

flash_m7_dfu() {
    if [ ! -f "$FIRMWARE_M7" ]; then
        echo -e "${RED}[ERROR] Firmware M7 no encontrado. Ejecuta: ./flash.sh build${NC}"
        return 1
    fi

    echo -e "${CYAN}[*] Flasheando M7 via DFU...${NC}"
    dfu-util -a 0 -D "$FIRMWARE_M7" -s 0x08000000:leave
    echo -e "${GREEN}[OK] M7 flasheado exitosamente${NC}"
}

flash_m4_dfu() {
    if [ ! -f "$FIRMWARE_M4" ]; then
        echo -e "${RED}[ERROR] Firmware M4 no encontrado. Ejecuta: ./flash.sh build${NC}"
        return 1
    fi

    echo -e "${CYAN}[*] Flasheando M4 via DFU...${NC}"
    dfu-util -a 0 -D "$FIRMWARE_M4" -s 0x08100000:leave
    echo -e "${GREEN}[OK] M4 flasheado exitosamente${NC}"
}

flash_west() {
    echo -e "${CYAN}[*] Flasheando via west (OpenOCD/ST-Link)...${NC}"

    # shellcheck source=/dev/null
    source ~/.venvs/zephyr/bin/activate 2>/dev/null || true
    export ZEPHYR_BASE=~/zephyr-ws/zephyr

    cd "$PROJECT_DIR"
    west flash
    echo -e "${GREEN}[OK] Firmware flasheado via west${NC}"
}

flash_monitor() {
    echo -e "${CYAN}[*] Abriendo monitor serial...${NC}"
    echo "  (Ctrl+A luego X para salir)"
    echo ""

    # Buscar puerto
    for p in /dev/ttyACM* /dev/ttyUSB*; do
        if [ -e "$p" ]; then
            echo -e "${GREEN}[+] Conectado a: ${p}${NC}"
            if command -v minicom &>/dev/null; then
                minicom -D "$p" -b 115200
            elif command -v screen &>/dev/null; then
                screen "$p" 115200
            elif command -v picocom &>/dev/null; then
                picocom -b 115200 "$p"
            else
                echo -e "${YELLOW}[*] No hay monitor serial instalado${NC}"
                echo "  Instalar: sudo pacman -S minicom"
                echo "            sudo pacman -S screen"
                echo "            sudo pacman -S picocom"
                echo ""
                echo "  O usar Zephyr: west build -t monitor"
            fi
            return 0
        fi
    done

    echo -e "${RED}[ERROR] No se encontro puerto serial${NC}"
    echo "  Conecta el Giga R1 por USB"
}

clean_build() {
    echo -e "${CYAN}[*] Limpiando directorios de build...${NC}"
    rm -rf "$BUILD_DIR" "$BUILD_M4_DIR"
    echo -e "${GREEN}[OK] Build limpiado${NC}"
}

print_help() {
    print_banner
    echo "Uso: ./flash.sh [opcion]"
    echo ""
    echo "Opciones:"
    echo "  (sin args)  Flashear M7 via DFU (recomendado)"
    echo "  m7          Flashear solo M7 via DFU"
    echo "  m4          Flashear solo M4 via DFU"
    echo "  both        Flashear M7 + M4 via DFU"
    echo "  west        Flashear via west + OpenOCD (ST-Link)"
    echo "  dfu         Mostrar instrucciones para modo DFU"
    echo "  build       Compilar firmware sin flashear"
    echo "  monitor     Abrir monitor serial (115200 baud)"
    echo "  clean       Limpiar build"
    echo "  help        Mostrar esta ayuda"
    echo ""
    echo "Requisitos:"
    echo "  - dfu-util (para flashear por USB)"
    echo "  - openocd (para flashear por ST-Link/SWD)"
    echo "  - Zephyr SDK (para compilar)"
    echo ""
    echo "Ejemplos:"
    echo "  ./flash.sh          # Compilar y flashear M7"
    echo "  ./flash.sh both     # Flashear ambos nucleos"
    echo "  ./flash.sh monitor  # Ver logs del dispositivo"
    echo ""
}

# ============================================================
# MAIN
# ============================================================

print_banner
check_deps

case "${1:-}" in
    m7)
        check_dfu || exit 1
        flash_m7_dfu
        ;;
    m4)
        check_dfu || exit 1
        flash_m4_dfu
        ;;
    both)
        check_dfu || exit 1
        flash_m7_dfu
        echo ""
        flash_m4_dfu
        ;;
    west)
        flash_west
        ;;
    dfu)
        enter_dfu
        ;;
    build)
        build_firmware
        ;;
    monitor)
        flash_monitor
        ;;
    clean)
        clean_build
        ;;
    help|-h|--help)
        print_help
        ;;
    "")
        # Default: build + flash M7 via DFU
        if [ ! -f "$FIRMWARE_M7" ]; then
            echo -e "${YELLOW}[*] Firmware no encontrado, compilando...${NC}"
            build_firmware
        fi
        check_dfu || exit 1
        flash_m7_dfu
        ;;
    *)
        echo -e "${RED}[ERROR] Opcion desconocida: $1${NC}"
        echo "  Ejecuta: ./flash.sh help"
        exit 1
        ;;
esac
