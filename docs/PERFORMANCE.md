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
| 0 Launcher | 17979 | 158260 | 0.1377 | 0.4121 | 2.99× |
| 1 Osciloscopio | 26917 | 137294 | 0.2062 | 0.3575 | 1.73× |
| 2 Fuente | 26444 | 126592 | 0.2025 | 0.3297 | 1.63× |
| 3 Flipper RF | 23396 | 135629 | 0.1792 | 0.3532 | 1.97× |
| 4 WiFi | 19769 | 167494 | 0.1514 | 0.4362 | 2.88× |
| 5 Ajustes | 15996 | 151745 | 0.1225 | 0.3952 | 3.23× |
| 6 Acerca de | 23674 | 142425 | 0.1813 | 0.3709 | 2.05× |

Lectura honesta: **el dibujo es más caro a propósito.** La interfaz pasó a
Material Design 3 — texto Roboto con antialiasing (mezcla alfa por píxel),
superficies tonales redondeadas, iconos de trazo — sobre una fuente bitmap 5×7
sin antialiasing. Parte del salto por frame son los 2.94× píxeles; ns/píxel ya
descuenta el tamaño, así que ese **1.6–3.2×** es el coste real del antialiasing y
los componentes. Es una regresión de rendimiento deliberada a cambio de una
interfaz que parece Android en vez de una terminal.

En absoluto sigue sobrado: el peor frame es **167 µs** (WiFi), un **0.5 %** del
presupuesto de 33 ms a 30 FPS. Para una pantalla que mira una persona, no un
bucle de datos, es la decisión correcta.

Sí se optimizó lo que el perfil señaló como cuello real, no lo que se suponía:
`m_ring_rect` recorriendo solo el perímetro salió **neutro** (0.96–1.05×, ruido).
Lo que sí costaba era la traza del osciloscopio: llamaba `m_line()` una vez por
columna y por canal, ~1500 llamadas por frame a 3.9 µs, cada una estampando
discos con `sqrtf`. Una curva muestreada por columna solo necesita un span
vertical entre muestras: **463 → 129 µs por frame, 3.6×**, y dibuja mejor las
transiciones verticales de la onda cuadrada.

### Hashes del framebuffer (baseline visual Material)

| Pantalla | Hash previo (480×272) | Hash Material (800×480) |
|---|---|---|
| 0 | `32348a1d` | `7b5a33b5` |
| 1 | `de6a4d72` | `84114911` |
| 2 | `f9760627` | `ae0af51a` |
| 3 | `c7676811` | `1e9b1290` |
| 4 | `083fde0c` | `3cf8780b` |
| 5 | `c180a63d` | `7d8bbd61` |
| 6 | `1d3962ed` | `e379454d` |

### Trampa de verificación que produjo falsos positivos

`cmake --build dirA dirB` **no es válido**: CMake responde `Unknown argument dirB`
y sale con código 1. Filtrando la salida con `grep -E 'error|warning'` ese mensaje
no contiene ninguna de las dos palabras, así que el comando parece limpio y el
`ctest` siguiente corre el **binario anterior**. Varias tandas de esta sesión
dieron verde sobre código sin compilar. Construir un directorio por invocación y
comprobar el código de salida (`set -e`), no la ausencia de la palabra "error".

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
