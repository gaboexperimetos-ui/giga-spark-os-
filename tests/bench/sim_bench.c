/* Exercise the real simulator drawing code without a window or frame limiter. */
#define main gigaspark_sim_entry
#include "main.c"
#undef main
#include <time.h>

#define FRAMES 3000u
#define REPEATS 5u
static volatile uint32_t sink;
static uint64_t now_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static uint32_t hash_frame(void)
{
	uint32_t h = UINT32_C(2166136261);
	for (size_t i = 0; i < sizeof(fb) / sizeof(fb[0]); ++i)
		h = (h ^ fb[i]) * UINT32_C(16777619);
	return h;
}
static void draw(int screen, uint32_t t)
{
	memset(fb, 0, sizeof(fb));
	switch (screen) {
	case 0: app_launch(t, 100); break;
	case 1: app_scope(t, 100); break;
	case 2: app_psu(t, 100); break;
	case 3: app_flip(t, 100); break;
	case 4: app_wifi(t, 100); break;
	case 5: app_set(t, 100); break;
	case 6: app_about(t, 100); break;
	}
}
int main(void)
{
	puts("screen,ns_per_frame,frames,hash_first,hash_last");
	for (int screen = 0; screen < 7; ++screen) {
		uint64_t best = UINT64_MAX;
		uint32_t first = 0, last = 0;
		for (unsigned repeat = 0; repeat < REPEATS; ++repeat) {
			sc_phase = 0;
			draw(screen, 1000);
			first = hash_frame();
			uint64_t start = now_ns();
			for (unsigned frame = 0; frame < FRAMES; ++frame) {
				draw(screen, 1000 + frame);
				if (frame == FRAMES - 1) last = hash_frame();
				sink += fb[frame % (SCR_W * SCR_H)];
			}
			uint64_t elapsed = now_ns() - start;
			if (elapsed < best) best = elapsed;
		}
		printf("%d,%.3f,%u,%08x,%08x\n", screen,
		       (double)best / FRAMES, FRAMES, first, last);
	}
	fprintf(stderr, "checksum=%u\n", sink);
	return 0;
}
