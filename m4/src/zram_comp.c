/*
 * Gigaspark OS - bounded RLE cache with LRU replacement
 *
 * Wire format unchanged: [count 1..255][byte] pairs. Incompressible data
 * expands 2x. This is a volatile cache: eviction discards data; it does NOT
 * provide persistent swap. Keep metadata contiguous for cheap searches.
 */
#include "zram_comp.h"
#include "bitops.h"

#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zram_comp, CONFIG_LOG_DEFAULT_LEVEL);

#define ZRAM_MAX_BLOCKS 32U
#define ZRAM_BLOCK_SIZE 512U
#define ZRAM_ALL_BLOCKS UINT32_MAX

struct zram_entry {
	uint32_t handle;
	uint32_t size_orig;
	uint32_t size_comp;
	uint32_t lru_counter;
	uint32_t in_use;
};

static struct zram_entry zram_table[ZRAM_MAX_BLOCKS];
static uint8_t zram_data[ZRAM_MAX_BLOCKS][ZRAM_BLOCK_SIZE];
static uint32_t zram_used_bits;
static uint32_t lru_global_counter;

static int lru_find(uint32_t handle)
{
	for (unsigned index = 0; index < ZRAM_MAX_BLOCKS; ++index) {
		if (zram_table[index].in_use && zram_table[index].handle == handle)
			return (int)index;
	}
	return -1;
}

/* At counter wrap, preserve relative order instead of making new entries old. */
static void lru_normalize(void)
{
	uint8_t ranks[ZRAM_MAX_BLOCKS] = {0};
	uint32_t bits = zram_used_bits;
	while (bits != 0) {
		unsigned i = giga_ctz32(bits);
		uint32_t others = zram_used_bits;
		uint8_t rank = 1;
		while (others != 0) {
			unsigned j = giga_ctz32(others);
			if (zram_table[j].lru_counter < zram_table[i].lru_counter) ++rank;
			others &= others - 1U;
		}
		ranks[i] = rank;
		bits &= bits - 1U;
	}
	bits = zram_used_bits;
	while (bits != 0) {
		unsigned i = giga_ctz32(bits);
		zram_table[i].lru_counter = ranks[i];
		bits &= bits - 1U;
	}
	lru_global_counter = ZRAM_MAX_BLOCKS;
}

static void lru_touch(unsigned index)
{
	if (lru_global_counter == UINT32_MAX) lru_normalize();
	zram_table[index].lru_counter = ++lru_global_counter;
}

static int lru_find_victim(void)
{
	int victim = -1;
	uint32_t oldest = UINT32_MAX;
	for (unsigned index = 0; index < ZRAM_MAX_BLOCKS; ++index) {
		if (zram_table[index].in_use && zram_table[index].lru_counter < oldest) {
			oldest = zram_table[index].lru_counter;
			victim = (int)index;
		}
	}
	return victim;
}

static void lru_remove(unsigned index)
{
	zram_used_bits &= ~(UINT32_C(1) << index);
	zram_table[index].in_use = 0;
}

static int lru_insert(uint32_t handle, const uint8_t *data,
		      uint32_t size_orig, uint32_t size_comp)
{
	if (size_comp > ZRAM_BLOCK_SIZE) return -1;
	int slot = lru_find(handle);
	if (slot < 0) {
		if (zram_used_bits == ZRAM_ALL_BLOCKS) {
			slot = lru_find_victim();
			LOG_WRN("LRU discard: handle=%u", zram_table[slot].handle);
			lru_remove((unsigned)slot);
		} else {
			slot = (int)giga_ctz32(~zram_used_bits);
		}
		zram_used_bits |= UINT32_C(1) << (unsigned)slot;
		zram_table[slot].in_use = 1;
		zram_table[slot].handle = handle;
	}
	zram_table[slot].size_orig = size_orig;
	zram_table[slot].size_comp = size_comp;
	memcpy(zram_data[slot], data, size_comp);
	lru_touch((unsigned)slot);
	return 0;
}

size_t zram_compress(const uint8_t *src, size_t src_len,
		     uint8_t *dst, size_t dst_len)
{
	if (src_len == 0) return 0;
	if (!src || !dst || dst_len < 2) return 0;
	size_t si = 0, di = 0;
	while (si < src_len) {
		uint8_t current = src[si];
		uint8_t count = 1;
		while (si + count < src_len && src[si + count] == current && count < 255)
			++count;
		if (dst_len - di < 2) return 0;
		dst[di++] = count;
		dst[di++] = current;
		si += count;
	}
	return di;
}

size_t zram_decompress(const uint8_t *src, size_t src_len,
		       uint8_t *dst, size_t dst_len)
{
	if (src_len == 0) return 0;
	/* Reject malformed input before writing anything. */
	if (!src || !dst || (src_len & 1U) != 0) return 0;
	size_t di = 0;
	for (size_t si = 0; si < src_len; si += 2) {
		size_t count = src[si];
		if (count == 0 || count > dst_len - di) return 0;
		memset(dst + di, src[si + 1], count);
		di += count;
	}
	return di;
}

size_t zram_compress_and_store(uint32_t handle, const uint8_t *src, size_t src_len)
{
	/* Handle zero is the eviction/not-found sentinel. */
	if (handle == 0 || !src || src_len == 0 || src_len > ZRAM_BLOCK_SIZE * 2U)
		return 0;
	uint8_t compressed[ZRAM_BLOCK_SIZE];
	size_t comp_len = zram_compress(src, src_len, compressed, sizeof(compressed));
	if (comp_len == 0 || lru_insert(handle, compressed, (uint32_t)src_len,
				      (uint32_t)comp_len) < 0)
		return 0;
	LOG_DBG("zRAM store: handle=%u orig=%u comp=%u", handle,
		(unsigned)src_len, (unsigned)comp_len);
	return comp_len;
}

size_t zram_retrieve(uint32_t handle, uint8_t *dst, size_t dst_len)
{
	int index = lru_find(handle);
	if (index < 0 || !dst) return 0;
	size_t len = zram_decompress(zram_data[index], zram_table[index].size_comp,
				     dst, dst_len);
	if (len != 0) lru_touch((unsigned)index);
	LOG_DBG("zRAM retrieve: handle=%u decomp=%u", handle, (unsigned)len);
	return len;
}

uint32_t zram_evict_block(uint32_t handle)
{
	int index = lru_find(handle);
	if (index < 0) return 0;
	uint32_t evicted = zram_table[index].handle;
	lru_remove((unsigned)index);
	return evicted;
}

void zram_get_stats(uint8_t *used, uint8_t *total, uint32_t *counter)
{
	if (used) {
		uint32_t bits = zram_used_bits;
		uint8_t count = 0;
		while (bits != 0) {
			++count;
			bits &= bits - 1U;
		}
		*used = count;
	}
	if (total) *total = (uint8_t)ZRAM_MAX_BLOCKS;
	if (counter) *counter = lru_global_counter;
}

void zram_reset(void)
{
	/* Invalid payload bytes need not be erased; only metadata is reachable. */
	memset(zram_table, 0, sizeof(zram_table));
	zram_used_bits = 0;
	lru_global_counter = 0;
}
