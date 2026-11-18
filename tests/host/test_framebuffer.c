#include "framebuffer.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

int main(void)
{
	uint16_t actual[32 * 17], expected[32 * 17];
	uint32_t seed = 42;
	for (unsigned test = 0; test < 10000; ++test) {
		seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
		int x = (int)(seed % 100) - 50;
		int y = (int)((seed >> 8) % 60) - 30;
		int w = (int)((seed >> 16) % 80);
		int h = (int)((seed >> 24) % 40);
		uint16_t color = (uint16_t)(seed >> 11);
		if (test % 3 == 0) color = 0xAAAA;
		memset(actual, 0x3C, sizeof(actual));
		memcpy(expected, actual, sizeof(actual));
		int cx = x, cy = y, cw = w, ch = h;
		int visible = giga_fb_clip(&cx, &cy, &cw, &ch, 32, 17);
		for (int row = y; row < y + h; ++row)
			for (int col = x; col < x + w; ++col)
				if (col >= 0 && col < 32 && row >= 0 && row < 17)
					expected[row * 32 + col] = color;
		if (visible)
			for (int row = 0; row < ch; ++row)
				giga_fb_span(actual + (cy + row) * 32 + cx, (size_t)cw, color);
		CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
	}
	int x = -31, y = -16, w = INT_MAX, h = INT_MAX;
	CHECK(giga_fb_clip(&x, &y, &w, &h, 32, 17));
	CHECK(x == 0 && y == 0 && w == 32 && h == 17);
	x = INT_MIN; y = INT_MIN; w = INT_MAX; h = INT_MAX;
	CHECK(!giga_fb_clip(&x, &y, &w, &h, 32, 17));
	x = INT_MAX; y = INT_MAX; w = 1; h = 1;
	CHECK(!giga_fb_clip(&x, &y, &w, &h, 32, 17));
	puts("framebuffer: 10000 clipped-span comparisons passed");
	return 0;
}
