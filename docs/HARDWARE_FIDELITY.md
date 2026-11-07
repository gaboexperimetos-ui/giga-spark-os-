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

## Discrepancias detectadas en el repositorio

El código usaba **480×272** y lo presentaba como el hardware real, atribuyéndolo
además a un panel **ILI9341** (que en realidad es de 320×240 y no monta este
shield). Al revisar salieron más afirmaciones incorrectas:

| Se afirmaba | Realidad verificada |
|---|---|
| Panel ILI9341, 480×272 | KD040WVFID026, 480×800, ST7701 |
| Pantalla "física vía LTDC" | MIPI DSI (el ST7701 no cuelga de LTDC) |
| Táctil "resistivo/capacitivo", FT6206 | Capacitivo Goodix GT911, 5 puntos |
| "Memoria 512 KB RAM" | 1 MB RAM en el MCU + 8 MB SDRAM externa |

### Ya corregido (comentarios y documentación)

Se corrigieron las afirmaciones falsas sin tocar constantes ni comportamiento:

- `simulator/main.c` — cabecera y título de sección: 480×272 queda explícitamente
  marcado como lienzo virtual, con la especificación real y por qué el ILI9341 no aplica.
- `m7/src/launcher.c` — deja de llamar "LTDC display" al lienzo virtual.
- `m7/src/nav_bar.h`, `m7/src/ui_manager.c` — dimensiones marcadas como virtuales.
- `m7/src/ui_manager.c` — el comentario del touch ahora dice GT911 (no FT6206), y
  el del flush aclara que el panel va por DSI, no por LTDC.
- `docs/SIMULATOR.md` — aviso explícito de que 480×272 no es el shield real, y la
  tabla de diferencias ahora usa resolución, bus, táctil y memoria correctos.

Verificado tras esos cambios: tests host 3/3, ASAN+UBSAN 3/3 y smoke del
simulador 1/1 en verde.

### Pendiente (requiere decisión aparte)

La **migración real a 800×480** no se hizo, porque cambia layout, framebuffer,
LVGL y los hashes de píxeles de los benchmarks:

- `simulator/main.c` — `SCR_W`/`SCR_H` y todo el layout dibujado a mano.
- `m7/src/nav_bar.h` — `SCREEN_WIDTH`/`SCREEN_HEIGHT` y `NAV_BAR_Y_START`.
- `m7/src/ui_manager.c` — `DISPLAY_WIDTH`/`DISPLAY_HEIGHT`.
- `m7/arduino_giga_r1_m7.overlay` — `displays = <&ltdc>`; el shield real se
  describe con el overlay `arduino_giga_display_shield` de Zephyr (ST7701 por DSI).
- `tests/bench/sim_bench.c` — los hashes de referencia cambian con la resolución.

Las cifras de la tabla "Metricas del Sistema" del `README.md` (flash/RAM usados,
número de tests) **no están verificadas**: no hay toolchain ARM ni workspace
Zephyr aquí, así que no se pudieron medir y se dejaron intactas.

## Alcance

Es un prototipo de interfaz en HTML para validar ergonomía táctil y proporciones.
No es el firmware corriendo en la placa, y no valida rendimiento ni consumo real.
