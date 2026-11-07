/* Portable RGB565 span primitives shared by the SDK and PC simulator.
 * The caller clips rectangles; these helpers never write outside a span.
 * No hardware-specific instructions or undefined pointer aliases are needed.
 */
#ifndef GIGASPARK_FRAMEBUFFER_H
#define GIGASPARK_FRAMEBUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline void giga_fb_span(uint16_t *dst, size_t pixels, uint16_t color)
{
	if (pixels == 0) return;
	if ((uint8_t)(color >> 8) == (uint8_t)color) {
		memset(dst, (uint8_t)color, pixels * sizeof(*dst));
		return;
	}

	/* A simple counted loop lets the compiler choose SIMD on the host and
	 * compact halfword stores on Cortex-M. No unaligned uint32_t aliasing. */
	for (size_t i = 0; i < pixels; ++i) dst[i] = color;

}

/* Clip without signed overflow, including INT_MIN/INT_MAX coordinates. */
static inline int giga_fb_clip(int *x, int *y, int *w, int *h,
			       int screen_w, int screen_h)
{
	if (*w <= 0 || *h <= 0 || *x >= screen_w || *y >= screen_h) return 0;
	if (*x < 0) {
		/* -w is safe because w > 0; do not negate INT_MIN x. */
		if (*x <= -*w) return 0;
		*w += *x;
		*x = 0;
	}
	if (*y < 0) {
		if (*y <= -*h) return 0;
		*h += *y;
		*y = 0;
	}
	if (*w > screen_w - *x) *w = screen_w - *x;
	if (*h > screen_h - *y) *h = screen_h - *y;
	return 1;
}

#endif /* GIGASPARK_FRAMEBUFFER_H */
