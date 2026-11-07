/* Puente local de vista previa: renderiza el simulador C real y expone
 * fotogramas RGB565 por stdout. No forma parte del firmware.
 *
 * Importa el reloj: handle_event() fija plazos con SDL_GetTicks() (resaltado
 * de pulsacion, fin del escaneo WiFi). Si el puente midiera el tiempo con
 * otro reloj, esos plazos nunca expirarian y el boton pulsado quedaria
 * resaltado para siempre. Por eso aqui se usa SDL_GetTicks() tambien.
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

int main(void) {
	int screen = 0;
	bool run = true;
	uint8_t bat = 100;

	SDL_SetMainReady();
	/* Solo el temporizador: render_screen() escribe en fb, no abre ventana. */
	if (SDL_Init(SDL_INIT_TIMER) < 0) {
		fprintf(stderr, "SDL_Init(timer): %s\n", SDL_GetError());
		return 1;
	}
	uint32_t start = SDL_GetTicks();
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

		uint32_t now = SDL_GetTicks();
		/* Expiracion de estados temporales, igual que el bucle SDL real. */
		if (press_live && (int32_t)(now - press_until) >= 0) press_live = false;
		if (wf_scan_until && (int32_t)(now - wf_scan_until) >= 0) wf_scan_until = 0;
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
	SDL_Quit();
	return 0;
}
