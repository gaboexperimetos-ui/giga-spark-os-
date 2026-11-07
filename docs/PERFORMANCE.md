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
| Dibujo de las 7 pantallas | — | — | aproximadamente igual; hashes de píxeles idénticos |
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
- Comparación automática de hashes de píxeles contra el baseline: coinciden.

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

python3 tests/bench/compare.py 6e5c14568ec66b11c9cc58114a8f24474831ef6f --runs 5
```
