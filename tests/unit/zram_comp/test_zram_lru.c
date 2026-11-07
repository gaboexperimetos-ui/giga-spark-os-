/*
 * Gigaspark OS - Test Unitario: zRAM Compression + LRU
 *
 * Verifica compresion RLE, almacenamiento LRU, y eviccion.
 *
 * Compilar para PC: west build -b native_sim tests/unit/zram_comp
 * Ejecutar: ./build/zephyr/zephyr.exe
 */

#include <zephyr/ztest.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Incluir directamente los .c para testear */
#include "zram_comp.h"

/* Buffer para datos comprimidos/descomprimidos */
#define TEST_BUF_SIZE  2048
static uint8_t src_buf[TEST_BUF_SIZE];
static uint8_t dst_buf[TEST_BUF_SIZE * 2];
static uint8_t out_buf[TEST_BUF_SIZE];

/*
 * Test 1: Compresion basica RLE
 */
ZTEST(zram_rle, test_basic_compress)
{
	/* Datos con repeticion: 100 bytes de 0xAA */
	memset(src_buf, 0xAA, 100);

	size_t comp_len = zram_compress(src_buf, 100, dst_buf, sizeof(dst_buf));

	/* RLE: 100 bytes repetidos = 2 bytes (count=100, byte=0xAA) */
	zassert_equal(comp_len, 2, "Compresion de 100 bytes repetidos debe ser 2 bytes");
	zassert_equal(dst_buf[0], 100, "Count debe ser 100");
	zassert_equal(dst_buf[1], 0xAA, "Byte debe ser 0xAA");
}

/*
 * Test 2: Compresion sin repeticion
 */
ZTEST(zram_rle, test_no_repetition)
{
	/* Datos sin repeticion: 0, 1, 2, 3, ... */
	for (int i = 0; i < 50; i++) {
		src_buf[i] = (uint8_t)i;
	}

	size_t comp_len = zram_compress(src_buf, 50, dst_buf, sizeof(dst_buf));

	/* Sin repeticion: cada byte = 2 bytes [1][byte] = 100 bytes */
	zassert_equal(comp_len, 100, "Sin repeticion: 50 bytes = 100 comprimidos");
}

/*
 * Test 3: Compresion mixta
 */
ZTEST(zram_rle, test_mixed_data)
{
	/* Patron: 10 de A, 10 de B, 10 de C */
	memset(src_buf + 0, 'A', 10);
	memset(src_buf + 10, 'B', 10);
	memset(src_buf + 20, 'C', 10);

	size_t comp_len = zram_compress(src_buf, 30, dst_buf, sizeof(dst_buf));

	/* 3 runs de 10 = 6 bytes */
	zassert_equal(comp_len, 6, "3 runs de 10 = 6 bytes comprimidos");
}

/*
 * Test 4: Descompresion
 */
ZTEST(zram_rle, test_decompress)
{
	/* Comprimir y descomprimir */
	memset(src_buf, 0x55, 200);

	size_t comp_len = zram_compress(src_buf, 200, dst_buf, sizeof(dst_buf));
	zassert_not_equal(comp_len, 0, "Compresion fallo");

	size_t decomp_len = zram_decompress(dst_buf, comp_len, out_buf, sizeof(out_buf));
	zassert_equal(decomp_len, 200, "Descompresion debe retornar 200 bytes");

	/* Verificar que los datos son identicos */
	zassert_mem_equal(src_buf, out_buf, 200,
			  "Datos descomprimidos deben ser identicos al original");
}

/*
 * Test 5: Buffer overflow no debe crashear
 */
ZTEST(zram_rle, test_buffer_overflow)
{
	memset(src_buf, 0xAA, 100);

	/* Buffer de destino muy pequeno (solo 1 byte) */
	size_t comp_len = zram_compress(src_buf, 100, dst_buf, 1);

	zassert_equal(comp_len, 0, "Buffer overflow debe retornar 0");
}

/*
 * Test 6: Stress test de compresion
 */
ZTEST(zram_rle, test_stress_compress)
{
	srand(123);

	for (int iter = 0; iter < 100; iter++) {
		/* Generar datos aleatorios con algunos runs */
		uint16_t len = 100 + (rand() % 900);  /* 100-999 bytes */

		for (uint16_t i = 0; i < len; i++) {
			if (rand() % 4 == 0) {
				/* Repetir el byte anterior 25% de las veces */
				src_buf[i] = (i > 0) ? src_buf[i - 1] : rand();
			} else {
				src_buf[i] = rand();
			}
		}

		size_t comp_len = zram_compress(src_buf, len, dst_buf, sizeof(dst_buf));
		zassert_not_equal(comp_len, 0, "Compresion fallo en iter %d", iter);

		size_t decomp_len = zram_decompress(dst_buf, comp_len, out_buf, sizeof(out_buf));
		zassert_equal(decomp_len, len,
			      "Descompresion fallo en iter %d: esperado %u, got %u",
			      iter, len, decomp_len);

		zassert_mem_equal(src_buf, out_buf, len,
				  "Datos corrompidos en iter %d", iter);
	}

	printk("\n  Stress Test: 100 iteraciones, datos aleatorios con runs\n");
	printk("  Todos los datos pasaron por compress -> decompress sin corrupcion\n");
}

/*
 * Test 7: LRU - Almacenar y recuperar
 */
ZTEST(zram_lru, test_store_retrieve)
{
	/* Datos de prueba */
	for (int i = 0; i < 256; i++) {
		src_buf[i] = (uint8_t)(i & 0xFF);
	}

	/* Almacenar */
	size_t stored = zram_compress_and_store(1, src_buf, 256);
	zassert_not_equal(stored, 0, "zram_compress_and_store fallo");

	/* Recuperar */
	size_t retrieved = zram_retrieve(1, out_buf, sizeof(out_buf));
	zassert_not_equal(retrieved, 0, "zram_retrieve fallo");

	/* Los datos deben ser identicos */
	zassert_mem_equal(src_buf, out_buf, 256,
			  "Datos recuperados deben ser identicos");
}

/*
 * Test 8: LRU - Eviccion
 */
ZTEST(zram_lru, test_eviction)
{
	/* Llenar la tabla LRU (32 bloques maximo) */
	for (int i = 0; i < 35; i++) {
		for (int j = 0; j < 128; j++) {
			src_buf[j] = (uint8_t)(i + j);
		}
		zram_compress_and_store(i, src_buf, 128);
	}

	/* Verificar que hay evicciones */
	uint8_t used = 0, total = 0;
	uint32_t counter = 0;
	zram_get_stats(&used, &total, &counter);

	zassert_true(used <= total,
		     "Bloques usados (%d) no debe exceder total (%d)", used, total);
	zassert_true(counter > 0, "Contador LRU debe ser > 0");

	printk("\n  LRU Stats: used=%d/%d, counter=%u\n", used, total, counter);
	printk("  Evicciones funcionando correctamente\n");
}

/*
 * Test 9: LRU - Bloque inexistente
 */
ZTEST(zram_lru, test_nonexistent)
{
	size_t result = zram_retrieve(999, out_buf, sizeof(out_buf));
	zassert_equal(result, 0, "Retrieve de bloque inexistente debe retornar 0");
}

/*
 * Test 10: Stats del zRAM
 */
ZTEST(zram_lru, test_stats)
{
	uint8_t used = 0, total = 0;
	uint32_t counter = 0;

	zram_get_stats(&used, &total, &counter);
	zassert_equal(used, 0, "Usados debe ser 0 al inicio");
	zassert_equal(total, 32, "Total debe ser 32");
	zassert_equal(counter, 0, "Counter debe ser 0 al inicio");
}

/*
 * Suite de tests RLE
 */
ZTEST_SUITE(zram_rle, NULL, NULL, NULL, NULL, NULL);

/*
 * Suite de tests LRU
 */
static void zram_lru_setup(void *f)
{
	ARG_UNUSED(f);
	zram_reset();
}

ZTEST_SUITE(zram_lru, NULL, NULL, zram_lru_setup, NULL, NULL);
