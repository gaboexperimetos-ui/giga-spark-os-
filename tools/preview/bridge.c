/* Puente local de vista previa: renderiza el simulador C real y expone
 * fotogramas RGB565 por stdout. No forma parte del firmware.
 */
#define SDL_MAIN_HANDLED
#define main gigaspark_simulator_main
#include "../../simulator/main.c"
#undef main

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

int main(void) {
	int screen = 0;
	bool run = true;
	uint8_t bat = 100;
	uint32_t start = now_ms();
	uint32_t last_battery = start;
	char line[128];

	setvbuf(stdout, NULL, _IONBF, 0);
	while (fgets(line, sizeof(line), stdin)) {
		if (line[0] == 'Q') {
			break;
		} else if (line[0] == 'K') {
			SDL_Event ev;
			memset(&ev, 0, sizeof(ev));
			ev.type = SDL_KEYDOWN;
			ev.key.keysym.sym = (SDL_Keycode)strtol(line + 1, NULL, 10);
			handle_event(&ev, &screen, &run);
			if (!run) { run = true; screen = 0; }
		} else if (line[0] == 'M') {
			int x = 0, y = 0;
			if (sscanf(line + 1, "%d %d", &x, &y) == 2) {
				SDL_Event ev;
				memset(&ev, 0, sizeof(ev));
				ev.type = SDL_MOUSEBUTTONDOWN;
				ev.button.button = SDL_BUTTON_LEFT;
				ev.button.x = x * SCALE;
				ev.button.y = y * SCALE;
				handle_event(&ev, &screen, &run);
			}
		}

		uint32_t now = now_ms();
		if (now - last_battery >= 30000u) {
			if (bat > 5) --bat;
			last_battery = now;
		}
		render_screen(screen, now - start, bat);
		uint32_t header[2] = { (uint32_t)screen, (uint32_t)bat };
		fwrite(header, sizeof(header), 1, stdout);
		fwrite(fb, sizeof(fb), 1, stdout);
		fflush(stdout);
	}
	return 0;
}
