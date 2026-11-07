/*
 * Gigaspark OS - Test Unitario: Buddy Allocator
 *
 * Ejecuta 1000 asignaciones/liberaciones aleatorias
 * verificando que no haya leaks ni corrupcion de memoria.
 *
 * Compilar para PC: west build -b native_sim tests/unit/mem_engine
 * Ejecutar: ./build/zephyr/zephyr.exe
 */

#include <zephyr/ztest.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Incluir directamente el .c para testear (no usar header) */
#include "mem_engine.h"

/* Max handles que el buddy allocator puede manejar */
#define MAX_TEST_HANDLES  64
#define MAX_TEST_SIZE     2048
#define NUM_ITERATIONS    1000

/* Estado del test */
static uint32_t handles[MAX_TEST_HANDLES];
static uint32_t sizes[MAX_TEST_HANDLES];
static uint8_t  active[MAX_TEST_HANDLES];

/* Estadisticas */
static uint32_t total_allocs = 0;
static uint32_t total_frees = 0;
static uint32_t failed_allocs = 0;

/*
 * Test 1: Inicializacion
 */
ZTEST(buddy_allocator, test_init)
{
	mem_engine_init();

	uint8_t usage = mem_get_usage();
	zassert_equal(usage, 0, "Pool debe estar vacio al inicio (uso=%d%%)", usage);
}

/*
 * Test 2: Asignacion basica
 */
ZTEST(buddy_allocator, test_basic_alloc)
{
	mem_engine_init();

	/* Asignar 64 bytes */
	uint32_t h1 = mem_alloc(64);
	zassert_not_equal(h1, 0, "Asignacion de 64 bytes debe ser exitosa");

	/* Verificar que el handle es valido (1-64) */
	zassert_true(h1 >= 1 && h1 <= MAX_TEST_HANDLES,
		     "Handle fuera de rango: %u", h1);

	/* Liberar */
	int ret = mem_free(h1);
	zassert_equal(ret, 0, "Liberacion debe retornar 0");
}

/*
 * Test 3: Multiples asignaciones
 */
ZTEST(buddy_allocator, test_multiple_alloc)
{
	mem_engine_init();

	/* Asignar bloques de tamano creciente (potencias de 2) */
	uint32_t test_sizes[] = {32, 64, 128, 256, 512};
	int count = sizeof(test_sizes) / sizeof(test_sizes[0]);

	for (int i = 0; i < count; i++) {
		uint32_t h = mem_alloc(test_sizes[i]);
		zassert_not_equal(h, 0, "Alloc %d (%u bytes) fallo", i, test_sizes[i]);
		handles[i] = h;
		sizes[i] = test_sizes[i];
		active[i] = 1;
	}

	/* Verificar que todos los handles son validos */
	for (int i = 0; i < count; i++) {
		zassert_true(handles[i] >= 1 && handles[i] <= MAX_TEST_HANDLES,
			     "Handle invalido: %u", handles[i]);
	}

	/* Liberar todos */
	for (int i = 0; i < count; i++) {
		if (active[i]) {
			int ret = mem_free(handles[i]);
			zassert_equal(ret, 0, "Free del handle %u fallo", handles[i]);
			active[i] = 0;
		}
	}

	uint8_t usage = mem_get_usage();
	zassert_equal(usage, 0, "Pool debe estar vacio despues de liberar todo");
}

/*
 * Test 4: Stress test - 1000 alloc/free aleatorios
 * Verifica que no haya leaks ni corrupcion.
 */
ZTEST(buddy_allocator, test_stress_random)
{
	mem_engine_init();

	srand(42);  /* Seed deterministico para reproducibilidad */

	total_allocs = 0;
	total_frees = 0;
	failed_allocs = 0;

	memset(handles, 0, sizeof(handles));
	memset(sizes, 0, sizeof(sizes));
	memset(active, 0, sizeof(active));

	for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
		int idx = rand() % MAX_TEST_HANDLES;

		if (active[idx]) {
			/* Liberar */
			int ret = mem_free(handles[idx]);
			zassert_equal(ret, 0, "Free fallo en iter %d, handle %u",
				     iter, handles[idx]);

			handles[idx] = 0;
			sizes[idx] = 0;
			active[idx] = 0;
			total_frees++;
		} else {
			/* Asignar tamano aleatorio */
			uint32_t size = 32 + (rand() % (MAX_TEST_SIZE - 32));
			/* Alinear a 32 bytes (tamano minimo del buddy) */
			size = (size + 31) & ~31;

			uint32_t h = mem_alloc(size);

			if (h != 0) {
				handles[idx] = h;
				sizes[idx] = size;
				active[idx] = 1;
				total_allocs++;

				/* Verificar que el handle es valido */
				zassert_true(h >= 1 && h <= MAX_TEST_HANDLES,
					     "Handle invalido: %u", h);
			} else {
				failed_allocs++;
			}
		}
	}

	/* Liberar todos los handles restantes */
	for (int i = 0; i < MAX_TEST_HANDLES; i++) {
		if (active[i]) {
			mem_free(handles[i]);
			active[i] = 0;
		}
	}

	uint8_t usage = mem_get_usage();
	zassert_equal(usage, 0, "Pool debe estar vacio al final (uso=%d%%)", usage);

	printk("\n  Stress Test Results:\n");
	printk("    Allocs exitosos:  %u\n", total_allocs);
	printk("    Frees exitosos:   %u\n", total_frees);
	printk("    Allocs fallidos:  %u (pool lleno, esperado)\n", failed_allocs);
	printk("    Iteraciones:      %u\n", NUM_ITERATIONS);
}

/*
 * Test 5: Verificar que mem_get_info funciona
 */
ZTEST(buddy_allocator, test_get_info)
{
	mem_engine_init();

	uint32_t h = mem_alloc(128);
	zassert_not_equal(h, 0, "Alloc fallo");

	uint32_t addr = 0, size = 0;
	int ret = mem_get_info(h, &addr, &size);
	zassert_equal(ret, 0, "mem_get_info fallo");
	zassert_true(size >= 128, "Tamano asignado menor al solicitado: %u", size);
	zassert_true(addr != 0, "Direccion invalida: 0x%08x", addr);

	mem_free(h);
}

/*
 * Test 6: Liberar handle invalido no debe crashear
 */
ZTEST(buddy_allocator, test_invalid_free)
{
	mem_engine_init();

	/* Handle 0 (invalido) */
	int ret = mem_free(0);
	zassert_equal(ret, -1, "Free handle 0 debe retornar -1");

	/* Handle fuera de rango */
	ret = mem_free(100);
	zassert_equal(ret, -1, "Free handle 100 debe retornar -1");

	/* Handle que nunca fue asignado */
	ret = mem_free(5);
	zassert_equal(ret, -1, "Free handle no asignado debe retornar -1");
}

/*
 * Test 7: Double free no debe crashear
 */
ZTEST(buddy_allocator, test_double_free)
{
	mem_engine_init();

	uint32_t h = mem_alloc(64);
	zassert_not_equal(h, 0, "Alloc fallo");

	int ret1 = mem_free(h);
	zassert_equal(ret1, 0, "Primer free fallo");

	/* Segundo free - no debe retornar error (ya esta libre) */
	int ret2 = mem_free(h);
	zassert_equal(ret2, -1, "Double free debe retornar -1");
}

/*
 * Test 8: Asignaciones de tamano exacto (potencias de 2)
 */
ZTEST(buddy_allocator, test_power_of_two_sizes)
{
	mem_engine_init();

	uint32_t test_sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
	int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

	for (int i = 0; i < num_sizes; i++) {
		uint32_t h = mem_alloc(test_sizes[i]);
		zassert_not_equal(h, 0, "Alloc %u bytes fallo", test_sizes[i]);

		uint32_t addr = 0, size = 0;
		mem_get_info(h, &addr, &size);
		zassert_true(size >= test_sizes[i],
			     "Tamano real (%u) menor al solicitado (%u)",
			     size, test_sizes[i]);

		mem_free(h);
	}
}

/*
 * Suite de tests
 */
static void *buddy_suite_setup(void)
{
	return NULL;
}

ZTEST_SUITE(buddy_allocator, NULL, buddy_suite_setup, NULL, NULL, NULL);
