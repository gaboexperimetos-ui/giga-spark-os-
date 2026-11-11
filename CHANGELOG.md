# Changelog

Formato basado en [Keep a Changelog](https://keepachangelog.com/es/1.1.0/).

El proyecto no publica versiones etiquetadas todavía: todo lo de abajo está en
`main` o en revisión. Las fechas son de los commits, no de releases.

## Sin publicar

### Añadido

- **CI verificado en la última ejecución: 8/8 jobs en verde** (`42721414186`).
  Las correcciones posteriores incluyeron `CONFIG_STM32_LTDC_RGB565`, Python 3.15
  con dependencias nativas para `hidapi`, token API para el checker del manifiesto,
  y mover `st_sel` al backend SDL para mantener `-Werror` limpio en ARM.
- **El firmware del M7 ya dibuja en el Arduino GIGA Display Shield.** Antes
  compilaba pero no mostraba nada: el hilo de UI era un `TODO` que parpadeaba un
  LED, `nav_bar.c` decía *"stub mode - no LVGL"* y `ui_manager.c` / `launcher.c`
  ni se compilaban. La interfaz Material solo existía en el simulador.
  Ahora `m7/src/display_giga.c` renderiza el mismo núcleo contra el panel ST7701
  por LTDC + MIPI-DSI y lee el touch GT911 por el subsistema `input`.
- **Núcleo de interfaz portable** en `common/ui_os.c`: 800×480 RGB565 sin
  dependencia de SDL ni de Zephyr. Lo incluyen el simulador y el firmware, así
  que no hay dos interfaces que se puedan divergir. Verificado con hash de los
  7 frames: idéntico antes y después de extraerlo.
- **Framebuffer en SDRAM.** 768 KB no caben en la SRAM interna del M7: van a los
  8 MB de `sdram1` (`ext-sdram` + sección del linker, `CONFIG_MEMC=y`).
- **Touch GT911 declarado en el overlay.** El shield de Zephyr solo habilita
  panel y micrófono. Pines tomados del esquemático ASX00039: I²C4 (PB6/PH12),
  INT PI1, nRST PI2, dirección `0x5D`.
- **`tools/ci/check_manifest.py`**: valida que cada nombre del allowlist de
  `west.yml` exista en el manifiesto de Zephyr. Un nombre inexistente no es un
  error para west: lo ignora en silencio y el síntoma sale mucho después como un
  header que falta. Verificado por mutación.
- **`tools/ci/annotate.sh`**: publica el error real como anotación de GitHub.
  Sin él un fallo solo deja *"Process completed with exit code 1"*, y los logs
  completos no son descargables desde todos los entornos.

- **Interfaz Material Design 3** en el simulador: Roboto con antialiasing (atlas
  de 4 bits, 122 glifos), superficies tonales, switches de pastilla, chips,
  slider, barra de estado y navegación Android de 3 botones.
- **Vista previa web** del simulador C real en `tools/preview/`: renderiza el
  framebuffer RGB565 y lo sirve por HTTP, con entrada táctil y de teclado. No
  necesita ventana ni display.
- **`west.yml`**: el manifiesto que faltaba. Fija Zephyr 4.5.0 e importa solo los
  8 módulos que usa el proyecto, entre ellos `cmsis_6` (el CMSIS de Cortex-M,
  distinto de `cmsis`) y `fatfs`, que el M4 necesita.
- **Tema visual canónico** en `common/include/ui_theme.h`, compartido entre el
  mismo núcleo de interfaz que usan el simulador y el firmware M7.
- **Soporte SDL3** (3.4.20) con respaldo en SDL2 (2.32.10) vía
  `simulator/sdl_compat.h`. Ambas rutas producen el mismo framebuffer.
- Benchmarks (`tests/bench/`), tests de host y suites de preview con
  verificación por mutación.
- Higiene del repo: `CONTRIBUTING.md`, `SECURITY.md`, plantillas de issue y PR,
  Dependabot, `.editorconfig`, `.gitattributes`, `.yamllint` y job de lint.
- Documentación: `docs/VERSIONS.md`, `docs/HARDWARE_FIDELITY.md`,
  `docs/PERFORMANCE.md`, `docs/SIMULATOR.md`.

### Cambiado

- **Zephyr 4.4.1 → 4.5.0**; SDK de Zephyr **1.0.1** (`arm-zephyr-eabi`) en lugar de
  `gcc-arm-none-eabi` de apt, que no es el toolchain que Zephyr espera.
- **Actions**: `checkout` y `upload-artifact` v4 → v7; `setup-python` v7 con
  Python 3.15 (3.13 fue una actualización intermedia, no la última). El CI
  avisaba de Node 20 deprecated y nadie lo había visto.
- Lienzo del simulador **480×272 → 800×480**, la resolución real del GIGA Display
  Shield en horizontal. Objetivos táctiles de ≥ 80 px (8.65 mm a 235 ppi).
- El **brillo** se modela como retroiluminación separada del framebuffer. El
  panel usa SWIRE en PB12 (no PWM); la cabecera del puente pasó de `<II` a `<III`.
- Traza del osciloscopio por spans verticales en vez de un `m_line()` por columna
  y canal: **463 → 129 µs/frame** (3.6×), medido con perfil real.
- Núcleo C: buddy allocator con bitmap de bloques libres, zRAM reescrito,
  rects recortados en el SDK. CPU en reposo del simulador **−77.6 %**.

### Arreglado

- **El CI de firmware nunca pudo funcionar**: `west init -l .` exige un
  manifiesto y `west.yml` no existía. Los tres jobs que lo usaban fallaban en
  "Install Zephyr SDK".
- **La vista previa no abría**: el servidor respondía **501** a `HEAD` y
  `OPTIONS`. Los proxies sondan con HEAD para decidir si el upstream vive, así
  que la marcaban como caída aunque `GET /` devolviera 200.
- **Tinte verde en las superficies oscuras**: tres cuantizaciones por truncamiento
  (paleta, mezcla alfa y brillo) igualaban los canales verde y azul en RGB565.
  Ahora todo redondea al más cercano.
- **Expansión RGB565 → 24 bits** no biyectiva: `(g<<2)|(g>>2)` fallaba en 28 de los
  64 verdes. Corregido a `(g<<2)|(g>>4)`; round-trip exacto en los 65536 valores.
- **Solapamientos de layout** en Ajustes y Fuente: controles posicionados desde
  abajo tapaban texto posicionado desde arriba.
- **Botones dibujados sin zona táctil** (HOME, info, bloqueo, refresh): se veían
  pero no respondían. Dibujo y `hit_add` van ahora en la misma llamada.
- **Aliasing de IDs de acción**: el enum contiguo se solapaba con `base+index`.
  Bases 100/120/140/160 con `_Static_assert` que impiden reintroducirlo.
- Pool base del buddy allocator, tamaño de compresión, clip con `INT_MIN` y wrap
  del LRU.
- **Glifos ausentes** que se dibujaban como `?` en silencio (`µ … – × ²`).
- Reloj del puente: usa `SDL_GetTicks()`, el mismo que la UI; con otro reloj los
  plazos de pulsación no expiraban nunca.
- Anillo antialiasado (`m_ring`) que no dibujaba nada por un rango vacío.

### Corregido en la documentación

El README afirmaba cosas que no eran ciertas. Se corrigieron:

- Badge estático de "Build Passing" cuando el CI estaba **rojo**. Ahora apunta al
  workflow real.
- "Zephyr 4.4.1", "C99" y "LVGL 9.5.0" escritos a mano. LVGL no expone versión en
  el pin que arrastra Zephyr: se documenta como tal en vez de inventar un número.
- "Tests 18/18 passing" y "Commits 23", cifras que no se correspondían con el repo.
- Resolución del panel: se había usado 480×272 como "hardware real". El shield
  ASX00039 es **480×800 nativo** (800×480 en horizontal), ST7701 sobre MIPI DSI,
  touch Goodix GT911.

### Límites conocidos

- El firmware M7/M4 compila en CI, pero no se ha probado en la placa física.
  Los timings, el encendido del panel, la orientación táctil y el brillo SWIRE
  requieren el Arduino GIGA Display Shield; ver `docs/HARDWARE_FIDELITY.md`.
- Los antiguos `m7/src/launcher.c` y `ui_manager.c` eran código muerto y fueron
  eliminados; la UI real reside en `m7/src/display_giga.c` + `common/ui_os.c`.
- El dibujo Material es **1.6–3.2× más caro por píxel** que la fuente bitmap
  anterior. Es deliberado: el peor frame son 167 µs, el 0.5 % del presupuesto a
  30 FPS.
