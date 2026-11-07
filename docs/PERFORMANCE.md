# Rendimiento — noviembre de 2026

## Punto de partida

El firmware y el simulador **ya estaban escritos en C**. Cambiar de lenguaje no
era una optimización pendiente. Se mantuvo C y se atacó trabajo repetido,
despertares innecesarios y recorridos de memoria.

Baseline: `6e5c14568ec66b11c9cc58114a8f24474831ef6f`.

## Cambios aplicados

- **Buddy allocator:** bitmaps de bloques libres, búsqueda acotada por 8 órdenes
  × 4 palabras, reserva de handle antes de dividir y contador de bytes usado.
  Se corrigió la pérdida de un bloque cuando se agotaban los 64 handles.
- **RLE/zRAM:** descompresión de runs con `memset`, metadatos separados de los
  payloads, validación de entradas malformadas y normalización LRU al agotar el
  contador de 32 bits. El formato comprimido no cambió.
- **M4:** espera directa en colas Zephyr, sin semáforos duplicados; el motor no
  procesa comandos antes de terminar la inicialización. Logs por comando en
  nivel debug. Configuración de compilación orientada a velocidad y logging
  diferido.
- **SDK:** llenado de rectángulos por filas y comprobaciones de límites sin
  desbordamiento de coordenadas.
- **Simulador:** las pantallas estáticas solo se redibujan cuando hay entrada o
  cambia el estado; el bucle espera eventos en lugar de renderizar 30 FPS sin
  necesidad. Las animaciones siguen limitadas a 30 FPS. Se añadió `--smoke`.
- **Build/tests:** CMake del simulador en Release por defecto, pruebas nativas,
  sanitizadores, benchmarks comparativos y job de CI nativo.

No se activó LTO ni se cambiaron relojes/cachés del STM32 sin medirlos en placa.

## Mediciones en PC

Entorno: Debian 12, x86_64, GCC 12.2, `-O3 -DNDEBUG`, SDL2 estático
(`cee83f2d9a916e9de30c5712e798541fdc134b06`). Cada benchmark usa el mejor tiempo
de 5 pasadas; la tabla es la mediana de 5 ejecuciones del benchmark.

| Prueba | Antes | Después |Resultado |
|---|---:|---:|---:|
| Buddy alloc + uso + free | 94.925 ns/op | 36.930 ns/op | **2.57×** |
| Buddy churn fragmentado | 75.837 ns/op | 11.382 ns/op | **6.66×** |
| RLE decompress sin repeticiones | 1350.135 ns/op | 1249.578 ns/op | **1.08×** |
| RLE decompress runs de 16 | 756.028 ns/op | 716.989 ns/op | **1.05×** |
| RLE compress / LRU | — | — | aproximadamente igual |
| Dibujo de las 7 pantallas | — | — | ver "Reescritura de la UI" más abajo: los hashes ya **no** coinciden |
| CPU del simulador estático en reposo | 14.46 % | 3.24 % | **77.6 % menos CPU** |

La medición de reposo usa el backend SDL `dummy`, tres segundos por ejecución y
cinco pares de mediciones de tiempo de CPU de procesos hijos. No representa
consumo eléctrico ni rendimiento de una pantalla física.

Los kernels de dibujo alternativos que resultaron más lentos en esta máquina no
se conservaron. La ganancia del simulador viene de evitar redraws innecesarios,
no de afirmar que cada primitive sea más rápida.

## Verificación realizada

- 200 000 operaciones modeladas de alloc/free, alineación, solapamiento y uso.
- 4096 round trips RLE, buffers cortos, entrada malformada, LRU y wrap de contador.
- 10 000 comparaciones de spans/recorte contra un modelo píxel a píxel.
- Smoke test SDL de las 7 pantallas.
- AddressSanitizer + UndefinedBehaviorSanitizer: aprobados.
- Comparación automática de hashes de píxeles contra el baseline: coincidían
  en aquella pasada. La reescritura de la UI los cambió deliberadamente; ver abajo.

## Reescritura de la UI a 800×480 (invalidación del baseline visual)

La interfaz se reescribió a **800×480** para coincidir con el GIGA Display Shield
real y ser usable con el dedo. Eso cambia el lienzo de **130560 a 384000 píxeles
(2.94×)**, así que **los hashes de píxeles del baseline ya no pueden coincidir**.
No es una regresión: es otro dibujo. Se deja constancia en vez de falsear la
comparación.

`tests/bench/compare.py` sigue abortando si los hashes cambian, salvo que se pase
`--allow-visual-change`. Esa bandera obliga a declarar el cambio visual y añade
una tabla de **ns/píxel**, porque comparar ns/frame entre lienzos de distinto
tamaño no dice nada: el mayor hace más trabajo por definición.

Medición real contra `6e5c1456`, GCC 12.2 `-O3 -DNDEBUG`, mediana de 5 ejecuciones
(mejor de 5 pasadas cada una):

| Pantalla | Antes ns/frame | Ahora ns/frame | ns/píxel antes | ns/píxel ahora | Coste por píxel |
|---|---:|---:|---:|---:|---:|
| 0 Launcher | 19260 | 72595 | 0.1475 | 0.1890 | 1.28× |
| 1 Osciloscopio | 27433 | 83824 | 0.2101 | 0.2183 | 1.04× |
| 2 Fuente | 28839 | 68435 | 0.2209 | 0.1782 | **0.81×** |
| 3 Flipper RF | 25493 | 67978 | 0.1953 | 0.1770 | **0.91×** |
| 4 WiFi | 20912 | 63640 | 0.1602 | 0.1657 | 1.04× |
| 5 Ajustes | 17179 | 68475 | 0.1316 | 0.1783 | 1.36× |
| 6 Acerca de | 26043 | 60611 | 0.1995 | 0.1578 | **0.79×** |

Lectura honesta: por frame todo es ~2.3–4× más lento porque hay 2.94× más píxeles.
Por píxel, **3 de 7 pantallas son más baratas** que antes y el resto sube entre un
4 % y un 36 % (tarjetas redondeadas, iconos y más texto). A 30 FPS el peor caso es
83.8 µs por frame, un **0.25 %** del presupuesto de 33 ms: el coste es irrelevante
para la fluidez y compraba objetivos táctiles de 8.6–10.4 mm en vez de 3.9 mm.

Los kernels de memoria no cambiaron en esta pasada; la variación respecto a la
tabla anterior (p. ej. buddy 3.13× vs 2.57×) es ruido de máquina entre sesiones, no
una mejora nueva. La cifra de reposo del 77.6 % sigue siendo válida: el bucle
continúa siendo event-driven (`SDL_WaitEventTimeout` + flag `dirty`) y solo las
pantallas animadas redibujan a 30 FPS.

**Nuevo baseline visual:** 800×480, 768000 bytes por frame. Para reproducir:

```bash
python3 tests/bench/compare.py 6e5c14568ec66b11c9cc58114a8f24474831ef6f \
  --runs 5 --allow-visual-change
```

## Verificación de la interfaz táctil

`tests/host/test_sim_ui.c` incluye el `main.c` real y comprueba **estado**, no
píxeles: que cada botón registrado mide ≥ 80 px y no se sale de la pantalla, que
cada mosaico abre su app, que los controles del osciloscopio/fuente/RF/WiFi cambian
las variables que deben, que los límites se recortan, que la paginación no ofrece
flechas inertes y que el overlay abre y cierra.

Ese test encontró un bug real que el smoke test no veía: los IDs de acción se
solapaban (`A_APP + índice` se tragaba los botones del osciloscopio, y
`A_RF_MODE + 2` caía en `A_RF_SCAN`), así que tocar una red WiFi bajaba el brillo.
Ahora cada grupo indexado tiene su rango reservado y hay `_Static_assert` que
impiden que vuelva a ocurrir.

## Límites importantes

- **No se compiló ni probó el firmware en el Arduino Giga R1** en este entorno;
  faltaba el workspace/Zephyr SDK. Las cifras de PC no deben extrapolarse al MCU.
- El proyecto todavía contiene drivers/stubs y afirmaciones de características no
  verificadas. Esta pasada no convierte esos stubs en hardware funcional.
- zRAM sigue siendo una caché volátil: una evicción descarta datos. El swap SD
  simulado no es almacenamiento persistente real.
- No se promete “100 % más rápido” ni “el máximo posible”: faltan perfiles en
  hardware, IPC real, SD, display y consumo.

## Reproducir

```bash
cmake -S tests/host -B build_host -DCMAKE_BUILD_TYPE=Release
cmake --build build_host
ctest --test-dir build_host --output-on-failure

cmake -S tests/host -B build_host_san -DCMAKE_BUILD_TYPE=Debug -DGIGASPARK_SANITIZE=ON
cmake --build build_host_san
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build_host_san --output-on-failure

cmake -S simulator -B build_simulator -DCMAKE_BUILD_TYPE=Release
cmake --build build_simulator
ctest --test-dir build_simulator --output-on-failure

# El lienzo cambio de 480x272 a 800x480: los hashes difieren a proposito.
python3 tests/bench/compare.py 6e5c14568ec66b11c9cc58114a8f24474831ef6f --runs 5 \
  --allow-visual-change

# Revision visual de las 7 pantallas (los tests en verde no detectan layout roto)
bash tools/preview/build.sh && python3 tools/preview/shots.py
```
