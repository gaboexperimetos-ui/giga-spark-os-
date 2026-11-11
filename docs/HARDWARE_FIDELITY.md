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
| Salida LTDC → DSI → panel | **RGB888** (exigido por el driver Zephyr) | `arduino_giga_r1_m7.overlay` |
| Framebuffer del núcleo | **RGB565**, 2 bytes/pixel | `CONFIG_STM32_LTDC_RGB565` |
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
| Panel conectado "directamente al LTDC" | LTDC → host MIPI DSI → ST7701; el controlador genera el framebuffer y el DSI lo transmite |
| Táctil "resistivo/capacitivo", FT6206 | Capacitivo Goodix GT911, 5 puntos |
| "Memoria 512 KB RAM" | 1 MB RAM en el MCU + 8 MB SDRAM externa |

### Primera pasada: correcciones históricas de comentarios y documentación

Aquella pasada corrigió las afirmaciones falsas **sin tocar constantes ni
comportamiento**: marcó 480×272 como lienzo virtual, dejó de llamar "LTDC display"
al panel, sustituyó FT6206 por GT911 y arregló la cifra de RAM. Fue necesaria pero
insuficiente: la interfaz seguía midiendo lo que no mide el hardware real. La
migración efectiva está debajo.

En la versión anterior, los comentarios estaban en `m7/src/launcher.c` y
`m7/src/ui_manager.c`. Esos módulos muertos se eliminaron en la migración al
backend real `m7/src/display_giga.c`. La ruta actual sí utiliza LTDC para el
scan-out, seguido del host MIPI DSI integrado.
- `docs/SIMULATOR.md` — resolución, bus, táctil y memoria correctos.

### Migración a 800×480: primero el simulador, luego el núcleo compartido

El simulador C (`simulator/main.c`) se reescribió a **800×480, escala 1:1**, con
layout pensado para el dedo y no para el ratón:

- `SCR_W 800`, `SCR_H 480`, `SCALE 1`. Framebuffer de **768000 bytes**.
- Cromado Android: barra de estado de 36 px, app bar de 80 px, área de contenido
de 284 px y navegación inferior de 80 px (ATRÁS / HOME / RECIENTES).
- **Todo objetivo táctil ≥ 80 px** de lado menor (8.6 mm). Un test lo verifica en
  las 7 pantallas.
- `btn()` dibuja y registra la zona táctil en la misma llamada, así que lo que se
  ve y lo que responde al dedo no pueden divergir. Los botones desactivados se
  dibujan apagados (`BS_OFF`) y no registran zona.
- Brillo modelado como retroiluminación independiente; la atenuación RGB565 no
  altera los píxeles del núcleo.

Verificado: tests host **4/4** (incluido el nuevo `test_sim_ui`), ASAN+UBSAN **4/4**,
smoke del simulador **1/1**, y toque real a través del puente web (mosaicos, +/−,
salida, HOME, overlay, paginación WiFi).

**Los tests en verde no bastaron.** Seis defectos de layout pasaban el smoke test y
solo aparecieron al renderizar las 7 pantallas a PNG y mirarlas: transiciones
verticales ausentes en CH2, ~100 px muertos en Fuente, el contador de página
solapando el borde de la tarjeta en WiFi, una flecha atrás con aspecto activo pero
inerte, una etiqueta encogida a escala 1 y hueco muerto en Acerca de. Para repetir
esa revisión: `python3 tools/preview/shots.py`.

### Firmware: ahora dibuja de verdad (verificado en CI)

**El cambio importante.** Hasta noviembre de 2026 el firmware del M7 compilaba
pero **no dibujaba nada**: el hilo de UI era un `TODO` que parpadeaba un LED
cada 500 ms, `nav_bar.c` decía literalmente *"stub mode - no LVGL"*, y
`ui_manager.c` / `launcher.c` ni estaban en `target_sources`. La interfaz
Material Design 3 solo existía en el simulador de escritorio.

Ahora el núcleo de interfaz es portable y **el mismo código** corre en los dos
sitios:

| Fichero | Papel |
|---|---|
| `common/ui_os.c` | Núcleo Material 800×480 RGB565. Sin SDL ni Zephyr. |
| `common/ui_material.c` | Primitivas de dibujo, mezcla alfa, tipografía. |
| `simulator/main.c` | Backend SDL (de 966 a 201 líneas). |
| `m7/src/display_giga.c` | Backend real: LTDC + ST7701 + GT911. |

**Prueba de que no cambió ni un píxel:** se compiló el código anterior desde
`git HEAD` y se compararon los 7 frames con `t=1000` y batería 100 fijos. Hash
FNV de los 768 000 bytes, idéntico antes y después:

| Pantalla | Hash | Zonas táctiles |
|---|---|---|
| 0 Launcher | `d4fd87490f778feb` | 8 |
| 1 Osciloscopio | `a8c8d1a78ea499d2` | 9 |
| 2 Fuente | `75d6884c78dad943` | 8 |
| 3 RF | `749fca146153ea42` | 12 |
| 4 WiFi | `c0568a78a1e11f6b` | 8 |
| 5 Ajustes | `c3dd2a7e3ec5853a` | 8 |
| 6 Acerca de | `55bba0450dc40646` | 3 |

#### Cadena de hardware

```
ui_os.c  ->  framebuffer RGB565 800x480 en SDRAM (768 KB)
         ->  LTDC (display-controller@50001000), scan-out por DMA
         ->  host MIPI-DSI, 2 lanes, salida RGB888
         ->  ST7701, panel KD040WVFID026-01-C025A de 3.97"
GT911 (I2C4, 0x5D)  ->  subsistema input  ->  hit_test / dispatch
```

Dos formatos que **no** hay que confundir: la *salida* del LTDC es RGB888
(el driver de Zephyr lo exige con un `#error`), mientras que el *framebuffer*
es RGB565 (`CONFIG_STM32_LTDC_RGB565`). El LTDC convierte por hardware, así que
el núcleo escribe los mismos bytes que en el simulador y ocupa 768 KB en vez de
1.5 MB.

#### Pines: del esquemático, no supuestos

Confirmados contra tres fuentes independientes: el esquemático oficial
[ASX00039](https://docs.arduino.cc/resources/schematics/ASX00039-schematics.pdf),
un overlay Zephyr funcional publicado en el foro de Arduino y los valores por
defecto de `Arduino_GigaDisplayTouch`.

| Señal | Pin |
|---|---|
| DSI RESET | PC6 |
| DSI TE | PI0 |
| Backlight | PB12 (**SWIRE**, un solo cable, no PWM) |
| Touch I²C | I²C4 — SCL PB6, SDA PH12 (compartido con la cámara) |
| Touch INT | PI1 |
| Touch nRST | PI2 |
| GT911 | `0x5D`, alternativo `0x14` |

**El shield de Zephyr no trae el touch.** Su overlay
(`boards/shields/arduino_giga_display_shield`) solo habilita el panel y el
micrófono; el GT911 se declara en `arduino_giga_r1_m7.overlay`.

El panel es nativo **480×800 vertical**. Se rota 90° (`rotation = <90>` en el
ST7701) y el touch se transforma en el Device Tree, no a mano:
`screen-width`/`screen-height` nativos + `display = <&ltdc>` + `swapped-x-y`,
que es lo que aplica `input_touchscreen_report_pos()`.

#### Memoria

800 × 480 × 2 = **768 KB**: no cabe en la SRAM interna del M7. Va a la SDRAM de
8 MB (`sdram1`) por dos vías: `ext-sdram` para el buffer del driver y una
sección del linker para el array del núcleo. `CONFIG_MEMC=y`.

Se usa `display_write()` en lugar de escribir directamente en el buffer del
driver: el M7 tiene D-cache y `display_write()` hace el flush necesario.
Escribir a mano sería más rápido y es justo el tipo de fallo que solo aparece
en hardware.

#### Qué está verificado y qué no

**Verificado en CI (8/8 jobs):** compila para ARM, encaja en memoria, y los
pines/nodos del Device Tree son válidos. El job publica `zephyr.bin` y
`zephyr.elf` para el M7 (el ZIP completo pesa 2 818 374 bytes; no es el tamaño
del firmware binario).

**No verificable sin la placa:**
- Los timings de LTDC y los divisores de PLL3/DSI están derivados de los del
  shield (en vertical, con los ejes intercambiados) y de la referencia de ST
  para el mismo SoC. Que el panel encienda con esos valores exactos requiere
  el hardware puesto.
- La dirección de la rotación (`inverted-y` + `swapped-x-y`) puede necesitar
  ajuste según cómo monte el panel el shield.
- El brillo real: PB12 es SWIRE, no PWM. El núcleo modela la retroiluminación
  aparte (`backlight_scale()`), pero aplicar atenuado real exige implementar
  el protocolo SWIRE.

**Defecto previo conservado:** `st_sel` (cursor de Ajustes con las flechas del
teclado) no afecta a ningún píxel, así que las flechas no resaltan nada en esa
pantalla. Se movió al backend de escritorio, que es donde hay teclado, sin
cambiar el dibujo para no romper la garantía de identidad visual.

## Alcance

Esta documentación cubre el simulador y el backend Zephyr compilado para la placa.
El CI valida compilación cruzada M7/M4; no sustituye una prueba física. No se
pueden afirmar encendido, timing, orientación táctil ni consumo real hasta probar
un Arduino GIGA con su Display Shield.
