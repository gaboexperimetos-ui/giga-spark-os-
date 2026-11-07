# Fidelidad con el hardware real (Arduino GIGA Display Shield)

La vista previa táctil (`prototype/touch-ui/`) se reconstruyó sobre las especificaciones
reales del shield, no sobre una pantalla genérica.

## Especificaciones verificadas

| Parámetro | Valor real | Fuente |
|---|---|---|
| Panel | KD040WVFID026-01-C025A, 3.97" TFT | Zephyr shield doc |
| Resolución nativa | **480 × 800** (portrait) | Arduino docs / Zephyr overlay |
| Landscape (rotado 90°) | **800 × 480** | derivado |
| Controlador | **ST7701**, MIPI DSI, 2 lanes | Zephyr overlay |
| Formato de color | **RGB565** (`MIPI_DSI_PIXFMT_RGB565`) | Zephyr overlay |
| Táctil | **Goodix GT911**, I²C, hasta 5 puntos | Arduino docs / Zephyr doc |
| Área activa | 86.47 × 51.88 mm (landscape) | derivado de 3.97" y 480:800 |
| Densidad | 235 ppi · **0.1081 mm/píxel** | derivado |

Fuentes:
- https://docs.arduino.cc/hardware/giga-display-shield/
- https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/shields/arduino_giga_display_shield/doc/index.rst
- https://github.com/zephyrproject-rtos/zephyr/blob/main/boards/shields/arduino_giga_display_shield/arduino_giga_display_shield.overlay

## Qué hace la vista previa

- Lienzo interno exacto de **800×480** (o **480×800** en orientación nativa).
- Selector de escala con modo **1:1 en milímetros reales**, usando una sonda CSS en `mm`
  para calibrar contra el monitor del usuario.
- Paleta **cuantizada a RGB565**: cada color CSS es exactamente representable en el
  framebuffer del panel, igual que el `uint16_t` del simulador C.
- **Auditoría táctil en mm**: mide cada botón y lo marca en rojo si su lado menor baja
  de 7 mm. Con `0.1081 mm/px`, 48 px son solo **5.2 mm** (insuficiente); por eso los
  objetivos son de 72–96 px (**7.8–10.4 mm**).
- Regla milimétrica opcional y foto del hardware real como referencia.

## Discrepancia detectada en el repositorio

El código actual usa **480×272** y lo atribuye a un panel **ILI9341**:

- `simulator/main.c:3` — "Resolucion exacta: 480x272 (Giga Display Shield ILI9341)"
- `simulator/main.c:18-19` — `SCR_W 480`, `SCR_H 272`
- `m7/src/nav_bar.h:16-17` — `SCREEN_WIDTH 480`, `SCREEN_HEIGHT 272`
- `m7/src/ui_manager.c:26-27` — `DISPLAY_WIDTH 480`, `DISPLAY_HEIGHT 272`
- `docs/SIMULATOR.md:5,92` — describe 480x272 como el hardware real

Eso **no corresponde** al GIGA Display Shield: el ILI9341 es un panel de 320×240,
y el shield real es 480×800 con ST7701 por DSI. Todavía no se cambió el firmware ni
el simulador C, porque eso afecta layout, framebuffer, LVGL y benchmarks, y conviene
decidirlo aparte. Esta vista previa ya muestra cómo se vería a resolución real.

## Alcance

Es un prototipo de interfaz en HTML para validar ergonomía táctil y proporciones.
No es el firmware corriendo en la placa, y no valida rendimiento ni consumo real.
