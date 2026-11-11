/*
 * Gigaspark OS - M4 bounded buddy allocator
 *
 * 4 KiB pool, 32..4096 byte blocks, at most 64 live handles.
 * Free-block bitmaps replace scans of individual blocks. Search is bounded
 * by 8 orders x 4 words; handle allocation and usage accounting are O(1).
 * All operations belong to the single M4 memory-engine thread.
 */
#include "mem_engine.h"
#include "bitops.h"

#include <string.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mem_engine, CONFIG_LOG_DEFAULT_LEVEL);

#define POOL_SIZE    4096U
#define MIN_ORDER    5U
#define MAX_ORDER    12U
#define NUM_ORDERS   (MAX_ORDER - MIN_ORDER + 1U)
#define MAX_HANDLES  64U
#define BLOCK_SIZE(order) (UINT32_C(1) << (order))
#define BLOCKS_AT(order)  (POOL_SIZE >> (order))

static uint8_t __attribute__((aligned(32))) pool[POOL_SIZE];
/* Only whole, unsplit free blocks are set. Unused high bits stay zero. */
static uint32_t free_bits[NUM_ORDERS][4];
static uint32_t handle_free_bits[2];
static uint32_t used_bytes;

struct handle_entry {
	uint32_t offset;
	uint32_t size;
	uint8_t order;
};
static struct handle_entry handles[MAX_HANDLES];

static void bitmap_set(uint32_t *words, unsigned index)
{
	words[index / 32U] |= UINT32_C(1) << (index % 32U);
}

static void bitmap_clear(uint32_t *words, unsigned index)
{
	words[index / 32U] &= ~(UINT32_C(1) << (index % 32U));
}

static int bitmap_test(const uint32_t *words, unsigned index)
{
	return (words[index / 32U] & (UINT32_C(1) << (index % 32U))) != 0;
}

static int find_free(unsigned min_order, unsigned *out_order, unsigned *out_index)
{
	for (unsigned order = min_order; order <= MAX_ORDER; ++order) {
		uint32_t *words = free_bits[order - MIN_ORDER];
		for (unsigned word = 0; word < 4; ++word) {
			if (words[word] != 0) {
				*out_order = order;
				*out_index = word * 32U + giga_ctz32(words[word]);
				return 0;
			}
		}
	}
	return -1;
}

static unsigned split_block(unsigned from_order, unsigned to_order, unsigned index)
{
	while (from_order > to_order) {
		bitmap_clear(free_bits[from_order - MIN_ORDER], index);
		index *= 2U;
		bitmap_set(free_bits[from_order - MIN_ORDER - 1U], index);
		bitmap_set(free_bits[from_order - MIN_ORDER - 1U], index + 1U);
		--from_order;
	}
	bitmap_clear(free_bits[to_order - MIN_ORDER], index);
	return index;
}

static void merge_block(unsigned order, unsigned index)
{
	while (order < MAX_ORDER) {
		unsigned buddy = index ^ 1U;
		if (!bitmap_test(free_bits[order - MIN_ORDER], buddy)) break;
		bitmap_clear(free_bits[order - MIN_ORDER], index);
		bitmap_clear(free_bits[order - MIN_ORDER], buddy);
		index /= 2U;
		++order;
		bitmap_set(free_bits[order - MIN_ORDER], index);
	}
}

static int alloc_handle(void)
{
	for (unsigned word = 0; word < 2; ++word) {
		if (handle_free_bits[word] != 0) {
			unsigned index = word * 32U + giga_ctz32(handle_free_bits[word]);
			bitmap_clear(handle_free_bits, index);
			return (int)index;
		}
	}
	return -1;
}

static int valid_handle(uint32_t handle)
{
	return handle > 0 && handle <= MAX_HANDLES &&
	       !bitmap_test(handle_free_bits, handle - 1U);
}

void mem_engine_init(void)
{
	memset(free_bits, 0, sizeof(free_bits));
	memset(handles, 0, sizeof(handles));
	handle_free_bits[0] = UINT32_MAX;
	handle_free_bits[1] = UINT32_MAX;
	used_bytes = 0;
	bitmap_set(free_bits[MAX_ORDER - MIN_ORDER], 0);
	LOG_INF("Buddy allocator: %u bytes, %u orders", POOL_SIZE, NUM_ORDERS);
}

uint32_t mem_alloc(uint32_t size)
{
	if (size == 0 || size > POOL_SIZE) return 0;
	unsigned target = MIN_ORDER;
	while (BLOCK_SIZE(target) < size) ++target;

	unsigned order, index;
	if (find_free(target, &order, &index) < 0) return 0;
	/* Reserve the handle BEFORE splitting: exhaustion must not leak a block. */
	int slot = alloc_handle();
	if (slot < 0) return 0;

	index = split_block(order, target, index);
	handles[slot].offset = index * BLOCK_SIZE(target);
	handles[slot].size = BLOCK_SIZE(target);
	handles[slot].order = (uint8_t)target;
	used_bytes += handles[slot].size;
	return (uint32_t)slot + 1U;
}

int mem_free(uint32_t handle)
{
	if (!valid_handle(handle)) return -1;
	unsigned slot = handle - 1U;
	unsigned order = handles[slot].order;
	unsigned index = handles[slot].offset >> order;
	used_bytes -= handles[slot].size;
	bitmap_set(handle_free_bits, slot);
	bitmap_set(free_bits[order - MIN_ORDER], index);
	merge_block(order, index);
	return 0;
}

int mem_get_info(uint32_t handle, uint32_t *addr, uint32_t *size)
{
	if (!valid_handle(handle)) return -1;
	const struct handle_entry *entry = &handles[handle - 1U];
	/* The firmware ABI is 32-bit. The intermediate uintptr_t also permits
	 * host tests on a 64-bit PC (where only the low address bits fit). */
	if (addr) *addr = (uint32_t)(uintptr_t)&pool[entry->offset];
	if (size) *size = entry->size;
	return 0;
}

uint8_t mem_get_usage(void)
{
	return (uint8_t)((used_bytes * 100U) / POOL_SIZE);
}

size_t mem_read(uint32_t handle, void *buf, size_t buf_size)
{
	if (!valid_handle(handle) || !buf) return 0;
	const struct handle_entry *entry = &handles[handle - 1U];
	size_t len = buf_size < entry->size ? buf_size : entry->size;
	memcpy(buf, &pool[entry->offset], len);
	return len;
}

int mem_write(uint32_t handle, const void *data, size_t data_len)
{
	if (!valid_handle(handle) || !data) return -1;
	const struct handle_entry *entry = &handles[handle - 1U];
	size_t len = data_len < entry->size ? data_len : entry->size;
	memcpy(&pool[entry->offset], data, len);
	return 0;
}
