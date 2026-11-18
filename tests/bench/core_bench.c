/* Deterministic CPU microbenchmarks; not a measurement of RTOS/SD/IPC latency. */
#include "mem_engine.h"
#include "zram_comp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define ROUNDS 20000u
#define REPEATS 5u
static uint8_t input[1024];
static uint8_t encoded[2048];
static uint8_t decoded[1024];
static volatile uint32_t sink;

static uint64_t now_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static void report(const char *name, uint64_t best, uint32_t operations)
{
	printf("%s,%.3f,%u\n", name, (double)best / operations, operations);
}

int main(void)
{
	uint64_t start, elapsed, best;
	puts("benchmark,ns_per_operation,operations");
	for (unsigned pattern = 0; pattern < 3; ++pattern) {
		for (size_t i = 0; i < sizeof(input); ++i)
			input[i] = pattern == 0 ? 0xA5 : pattern == 1 ? (uint8_t)i : (uint8_t)(i / 16);
		size_t len = zram_compress(input, sizeof(input), encoded, sizeof(encoded));
		if (!len) return 1;
		best = UINT64_MAX;
		for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
			start = now_ns();
			for (unsigned i = 0; i < ROUNDS; ++i) {
				encoded[0] = 0;
				sink += (uint32_t)zram_compress(input, sizeof(input), encoded, sizeof(encoded));
			}
			elapsed = now_ns() - start;
			if (elapsed < best) best = elapsed;
		}
		char name[40];
		snprintf(name, sizeof(name), "rle_compress_%u", pattern);
		report(name, best, ROUNDS);
		best = UINT64_MAX;
		for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
			start = now_ns();
			for (unsigned i = 0; i < ROUNDS; ++i) {
				decoded[0] ^= 1;
				sink += (uint32_t)zram_decompress(encoded, len, decoded, sizeof(decoded));
			}
			elapsed = now_ns() - start;
			if (elapsed < best) best = elapsed;
		}
		snprintf(name, sizeof(name), "rle_decompress_%u", pattern);
		report(name, best, ROUNDS);
	}
	memset(input, 0xA5, 128);
	best = UINT64_MAX;
	for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
		zram_reset();
		start = now_ns();
		for (unsigned i = 0; i < ROUNDS; ++i) {
			uint32_t handle = 1 + i % 32;
			if (!zram_compress_and_store(handle, input, 128)) return 7;
			sink += (uint32_t)zram_retrieve(handle, decoded, sizeof(decoded));
		}
		elapsed = now_ns() - start;
		if (elapsed < best) best = elapsed;
	}
	report("lru_store_retrieve", best, ROUNDS);
	best = UINT64_MAX;
	for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
		mem_engine_init();
		start = now_ns();
		for (unsigned i = 0; i < ROUNDS; ++i) {
			uint32_t h = mem_alloc(32 + (i % 8) * 32);
			if (!h) return 2;
			sink += mem_get_usage();
			if (mem_free(h) != 0) return 3;
		}
		elapsed = now_ns() - start;
		if (elapsed < best) best = elapsed;
	}
	report("buddy_alloc_usage_free", best, ROUNDS);
	best = UINT64_MAX;
	for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
		mem_engine_init();
		uint32_t handles[64];
		for (unsigned i = 0; i < 64; ++i) {
			handles[i] = mem_alloc(32);
			if (!handles[i]) return 4;
		}
		start = now_ns();
		for (unsigned i = 0; i < ROUNDS; ++i) {
			unsigned slot = i % 64;
			if (mem_free(handles[slot]) != 0) return 5;
			handles[slot] = mem_alloc(32);
			if (!handles[slot]) return 6;
			sink += mem_get_usage();
		}
		elapsed = now_ns() - start;
		if (elapsed < best) best = elapsed;
		for (unsigned i = 0; i < 64; ++i) mem_free(handles[i]);
	}
	report("buddy_fragmented_churn", best, ROUNDS);
	fprintf(stderr, "checksum=%u\n", sink);
	return 0;
}
