/* Small portable bit operations. Caller must pass a nonzero mask to ctz. */
#ifndef GIGASPARK_BITOPS_H
#define GIGASPARK_BITOPS_H

#include <stdint.h>

static inline unsigned giga_ctz32(uint32_t mask)
{
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned)__builtin_ctz(mask);
#else
	unsigned bit = 0;
	while ((mask & UINT32_C(1)) == 0) {
		mask >>= 1;
		++bit;
	}
	return bit;
#endif
}

#endif /* GIGASPARK_BITOPS_H */
