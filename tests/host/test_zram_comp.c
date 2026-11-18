/* Include the implementation to test counter-wrap without a production test hook. */
#include "zram_comp.c"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static uint8_t source[1024], encoded[2048], decoded[1024], scratch[2048];
static uint32_t seed = 123;
static uint32_t random32(void) { seed = seed * UINT32_C(1664525) + UINT32_C(1013904223); return seed; }

static void round_trip(size_t len, size_t capacity)
{
	size_t comp = zram_compress(source, len, encoded, sizeof(encoded));
	CHECK(comp != 0 && comp <= sizeof(encoded));
	memset(decoded, 0x5A, sizeof(decoded));
	CHECK(zram_decompress(encoded, comp, decoded, capacity) == len);
	CHECK(memcmp(source, decoded, len) == 0);
	for (size_t i = len; i < sizeof(decoded); ++i) CHECK(decoded[i] == 0x5A);
}

int main(void)
{
	zram_reset();
	CHECK(zram_compress(NULL, 1, encoded, sizeof(encoded)) == 0);
	CHECK(zram_compress(source, 1, NULL, sizeof(encoded)) == 0);
	CHECK(zram_compress(source, 0, encoded, sizeof(encoded)) == 0);
	CHECK(zram_decompress(encoded, 1, decoded, sizeof(decoded)) == 0);
	CHECK(zram_decompress(NULL, 2, decoded, sizeof(decoded)) == 0);
	CHECK(zram_compress_and_store(0, source, 1) == 0);
	memset(source, 0xA5, 300);
	CHECK(zram_compress(source, 300, encoded, sizeof(encoded)) == 4);
	CHECK(encoded[0] == 255 && encoded[1] == 0xA5 && encoded[2] == 45);
	round_trip(300, sizeof(decoded));
	memset(decoded, 0x5A, sizeof(decoded));
	CHECK(zram_decompress(encoded, 4, decoded, 299) == 0);
	for (size_t i = 255; i < sizeof(decoded); ++i) CHECK(decoded[i] == 0x5A);
	const uint8_t bad[] = {0, 1, 2, 3};
	memset(decoded, 0x5A, sizeof(decoded));
	CHECK(zram_decompress(bad, sizeof(bad), decoded, sizeof(decoded)) == 0);
	CHECK(decoded[0] == 0x5A);

	for (unsigned pattern = 0; pattern < 4; ++pattern) {
		for (size_t len = 1; len <= sizeof(source); ++len) {
			for (size_t i = 0; i < len; ++i)
				source[i] = pattern == 0 ? 0xFF : pattern == 1 ? (uint8_t)i :
					    pattern == 2 ? (uint8_t)(i / 17) : (uint8_t)random32();
			round_trip(len, sizeof(decoded));
			size_t comp = zram_compress(source, len, encoded, sizeof(encoded));
			for (size_t cap = 0; cap < comp; ++cap) {
				/* Only check selected capacities for incompressible long inputs. */
				if (cap > 4 && cap + 7 < comp && cap % 7 != 0) continue;
				CHECK(zram_compress(source, len, scratch, cap) == 0);
			}
		}
	}
	/* Store limit is unchanged; payloads that expand beyond 512 bytes fail. */
	for (size_t len = 1; len <= 1024; ++len) {
		memset(source, (int)len, len);
		size_t comp = zram_compress_and_store(1, source, len);
		CHECK(comp != 0);
		CHECK(zram_retrieve(1, decoded, sizeof(decoded)) == len);
		CHECK(memcmp(source, decoded, len) == 0);
	}
	for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)i;
	CHECK(zram_compress_and_store(1, source, sizeof(source)) == 0);
	memset(source, 0xEE, 128);
	CHECK(zram_retrieve(1, decoded, 127) == 0);
	CHECK(zram_retrieve(999, decoded, sizeof(decoded)) == 0);

	zram_reset();
	for (uint32_t h = 1; h <= 32; ++h) CHECK(zram_compress_and_store(h, source, 128) != 0);
	CHECK(zram_retrieve(1, decoded, sizeof(decoded)) == 128);
	CHECK(zram_compress_and_store(33, source, 128) != 0);
	CHECK(zram_retrieve(2, decoded, sizeof(decoded)) == 0);
	CHECK(zram_retrieve(1, decoded, sizeof(decoded)) == 128);
	CHECK(zram_retrieve(33, decoded, sizeof(decoded)) == 128);
	CHECK(zram_evict_block(1) == 1 && zram_evict_block(1) == 0);
	uint8_t used, total;
	uint32_t counter;
	zram_get_stats(&used, &total, &counter);
	CHECK(used == 31 && total == 32 && counter > 0);

	/* Force the rare wrap path; the touched entry must become newest. */
	lru_global_counter = UINT32_MAX - 1;
	CHECK(zram_retrieve(3, decoded, sizeof(decoded)) == 128);
	CHECK(zram_retrieve(4, decoded, sizeof(decoded)) == 128);
	CHECK(zram_compress_and_store(34, source, 128) != 0);
	CHECK(zram_compress_and_store(35, source, 128) != 0);
	CHECK(zram_retrieve(3, decoded, sizeof(decoded)) == 128);
	CHECK(zram_retrieve(4, decoded, sizeof(decoded)) == 128);
	CHECK(zram_retrieve(5, decoded, sizeof(decoded)) == 0);
	zram_reset();
	zram_get_stats(&used, &total, &counter);
	CHECK(used == 0 && counter == 0);
	puts("zram: 4096 round trips, short buffers, malformed RLE, LRU and counter wrap passed");
	return 0;
}
