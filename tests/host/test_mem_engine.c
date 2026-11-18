#include "mem_engine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define N 64
static uint32_t handles[N], requested[N];
static uint32_t seed = 42;
static uint32_t pool_base;
static void reset_model(void) {
	mem_engine_init();
	uint32_t h = mem_alloc(4096);
	CHECK(h && mem_get_info(h, &pool_base, NULL) == 0);
	CHECK(mem_free(h) == 0);
}
static uint32_t random32(void) { seed = seed * UINT32_C(1664525) + UINT32_C(1013904223); return seed; }

static void check_pool(void)
{
	uint8_t occupied[4096] = {0};
	uint32_t used = 0;
	/* All live spans must be aligned, disjoint and inside the pool. */
	for (unsigned i = 0; i < N; ++i) {
		if (!handles[i]) continue;
		uint32_t addr, size;
		CHECK(mem_get_info(handles[i], &addr, &size) == 0);
		CHECK(size >= requested[i] && size <= 4096);
		CHECK((size & (size - 1U)) == 0);
		CHECK(addr >= pool_base);
		size_t offset = addr - pool_base;
		CHECK(offset + size <= sizeof(occupied));
		CHECK(offset % size == 0);
		for (size_t j = offset; j < offset + size; ++j) CHECK(occupied[j]++ == 0);
		used += size;
	}
	CHECK(mem_get_usage() == used * 100U / sizeof(occupied));
}

int main(void)
{
	reset_model();
	CHECK(mem_get_usage() == 0);
	CHECK(mem_alloc(0) == 0 && mem_alloc(4097) == 0 && mem_alloc(UINT32_MAX) == 0);
	CHECK(mem_free(0) == -1 && mem_free(65) == -1 && mem_free(UINT32_MAX) == -1);
	CHECK(mem_read(1, NULL, 0) == 0 && mem_write(1, NULL, 0) == -1);

	/* Regression: exhausting handles must not consume/split the free block. */
	for (unsigned i = 0; i < N; ++i) { handles[i] = mem_alloc(1); CHECK(handles[i]); }
	CHECK(mem_alloc(1) == 0);
	CHECK(mem_get_usage() == 50);
	for (unsigned i = 0; i < N; ++i) CHECK(mem_free(handles[i]) == 0);
	CHECK(mem_get_usage() == 0);
	uint32_t whole = mem_alloc(4096);
	CHECK(whole != 0);
	CHECK(mem_free(whole) == 0 && mem_free(whole) == -1);

	const uint32_t sizes[] = {1, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255,
				 256, 257, 511, 512, 513, 1023, 1024, 1025, 2048, 2049, 4096};
	for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); ++s) {
		mem_engine_init();
		uint32_t h = mem_alloc(sizes[s]);
		CHECK(h != 0);
		uint32_t size;
		CHECK(mem_get_info(h, NULL, &size) == 0 && size >= sizes[s]);
		uint8_t data[4096], out[4096];
		memset(data, (int)(s + 1), sizes[s]);
		CHECK(mem_write(h, data, sizes[s]) == 0);
		CHECK(mem_read(h, out, sizeof(out)) == size);
		CHECK(memcmp(data, out, sizes[s]) == 0);
		CHECK(mem_write(h, data, sizeof(data)) == 0); /* existing bounded-write ABI */
		CHECK(mem_free(h) == 0);
		CHECK(mem_read(h, out, sizeof(out)) == 0);
		CHECK(mem_get_usage() == 0);
	}

	/* Deterministic model: every allocation is filled, every free coalesces. */
	for (unsigned repeat = 0; repeat < 100; ++repeat) {
		reset_model();
		memset(handles, 0, sizeof(handles));
		for (unsigned step = 0; step < 2000; ++step) {
			unsigned slot = random32() % N;
			if (handles[slot]) {
				uint8_t out[4096];
				size_t len = mem_read(handles[slot], out, sizeof(out));
				CHECK(len >= requested[slot]);
				for (size_t i = 0; i < requested[slot]; ++i) CHECK(out[i] == (uint8_t)(slot + 1));
				CHECK(mem_free(handles[slot]) == 0);
				handles[slot] = 0;
			} else {
				requested[slot] = 1 + random32() % 512;
				handles[slot] = mem_alloc(requested[slot]);
				if (handles[slot]) {
					uint8_t data[512];
					memset(data, (int)slot + 1, requested[slot]);
					CHECK(mem_write(handles[slot], data, requested[slot]) == 0);
				}
			}
			check_pool();
		}
		for (unsigned i = 0; i < N; ++i) if (handles[i]) CHECK(mem_free(handles[i]) == 0);
		CHECK(mem_get_usage() == 0);
		whole = mem_alloc(4096);
		CHECK(whole != 0);
		CHECK(mem_free(whole) == 0);
	}
	puts("mem_engine: boundaries, handle exhaustion, 200000 modeled operations passed");
	return 0;
}
