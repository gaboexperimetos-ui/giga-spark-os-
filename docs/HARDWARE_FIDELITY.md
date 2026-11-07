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

### Primera pasada: solo comentarios y documentación

Aquella pasada corrigió las afirmaciones falsas **sin tocar constantes ni
comportamiento**: marcó 480×272 como lienzo virtual, dejó de llamar "LTDC display"
al panel, sustituyó FT6206 por GT911 y arregló la cifra de RAM. Fue necesaria pero
insuficiente: la interfaz seguía midiendo lo que no mide el hardware real. La
migración efectiva está debajo.

- `m7/src/launcher.c` — deja de llamar "LTDC display" al lienzo virtual.
- `m7/src/ui_manager.c` — el comentario del touch ahora dice GT911 (no FT6206), y
  el del flush aclara que el panel va por DSI, no por LTDC.
- `docs/SIMULATOR.md` — resolución, bus, táctil y memoria correctos.

### Migración a 800×480: hecha en el simulador

El simulador C (`simulator/main.c`) se reescribió a **800×480, escala 1:1**, con
layout pensado para el dedo y no para el ratón:

- `SCR_W 800`, `SCR_H 480`, `SCALE 1`. Framebuffer de **768000 bytes**.
- Barra de estado de 56 px con el título de la pantalla activa; riel lateral de
  96 px con ATRÁS / HOME / MENU, siempre en el mismo sitio.
- **Todo objetivo táctil ≥ 80 px** de lado menor (8.6 mm). Un test lo verifica en
  las 7 pantallas.
- `btn()` dibuja y registra la zona táctil en la misma llamada, así que lo que se
  ve y lo que responde al dedo no pueden divergir. Los botones desactivados se
  dibujan apagados (`BS_OFF`) y no registran zona.
- Atenuación de brillo en RGB565, igual que haría el panel.

Verificado: tests host **4/4** (incluido el nuevo `test_sim_ui`), ASAN+UBSAN **4/4**,
smoke del simulador **1/1**, y toque real a través del puente web (mosaicos, +/−,
salida, HOME, overlay, paginación WiFi).

**Los tests en verde no bastaron.** Seis defectos de layout pasaban el smoke test y
solo aparecieron al renderizar las 7 pantallas a PNG y mirarlas: transiciones
verticales ausentes en CH2, ~100 px muertos en Fuente, el contador de página
solapando el borde de la tarjeta en WiFi, una flecha atrás con aspecto activo pero
inerte, una etiqueta encogida a escala 1 y hueco muerto en Acerca de. Para repetir
esa revisión: `python3 tools/preview/shots.py`.

### Firmware: constantes alineadas, sin verificar en placa

Estos cambios son de código fuente y **no se pudieron compilar** (no hay toolchain
ARM ni workspace Zephyr en este entorno):

- `m7/src/nav_bar.h` — `SCREEN_WIDTH/HEIGHT` a 800×480; barra de 40 → **80 px**
  (4.3 → 8.6 mm); `NAV_BAR_Y_START` y el botón HOME derivados de la geometría.
- `m7/src/nav_bar.c` — la zona del HOME dejó de usar los literales `190..290`,
  válidos solo para 480 px de ancho.
- `m7/src/ui_manager.c` — `DISPLAY_WIDTH/HEIGHT` toman `SCREEN_WIDTH/HEIGHT` de
  `nav_bar.h` (añadido el include) para que no puedan divergir.
- `m7/src/launcher.c` — de una columna de botones de 36 px (3.9 mm, y 8 apps se
  salían de la pantalla) a una rejilla de 2 columnas con tiles de 378×87 px
  (**9.4 mm**), calculada desde la geometría.

### Sigue pendiente

- `m7/arduino_giga_r1_m7.overlay` — `displays = <&ltdc>`; el shield real se
  describe con el overlay `arduino_giga_display_shield` de Zephyr (ST7701 por DSI).
  Cambiarlo a ciegas, sin poder compilar ni probar en placa, sería peor que
  dejarlo documentado.
- Compilar y validar el firmware en el hardware real.
- Los hashes de píxeles del benchmark cambiaron con la resolución; ver
  `docs/PERFORMANCE.md`.

Las cifras de la tabla "Metricas del Sistema" del `README.md` (flash/RAM usados,
número de tests) **no están verificadas**: no hay toolchain ARM ni workspace
Zephyr aquí, así que no se pudieron medir y se dejaron intactas.

## Alcance

Es un prototipo de interfaz en HTML para validar ergonomía táctil y proporciones.
No es el firmware corriendo en la placa, y no valida rendimiento ni consumo real.
