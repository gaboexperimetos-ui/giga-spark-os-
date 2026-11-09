/*
 * Gigaspark OS - Simulador SDL (backend de escritorio)
 *
 * La interfaz vive en common/ui_os.c y es portable: este fichero solo aporta
 * ventana, reloj y traduccion de eventos. El M7 usa el mismo nucleo contra el
 * panel real, asi que el simulador no es una maqueta aparte.
 *
 * Resolucion 800x480 landscape, la del Arduino GIGA Display Shield
 * (panel KD040WVFID026 de 3.97", ST7701 por MIPI DSI, touch GT911).
 * Ver docs/HARDWARE_FIDELITY.md.
 */

#include "sdl_compat.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SCALE  1
#define WIN_W  (SCR_W * SCALE)
#define WIN_H  (SCR_H * SCALE)
#define FPS    30
#define FRAME  (1000 / FPS)

#define UI_TICKS() gsp_ticks()
#include "../common/ui_os.c"

static bool handle_event(const SDL_Event *ev, int *screen, bool *run) {
	/* SDL2 agrupaba los eventos de ventana en SDL_WINDOWEVENT; SDL3 emite
	 * uno por caso. gsp_window_needs_redraw() abstrae la diferencia. */
	if (gsp_window_needs_redraw(ev)) return true;
	switch (ev->type) {
	case GSP_EV_QUIT: *run = false; return false;
	case GSP_EV_KEYDOWN:
		switch (gsp_event_key(ev)) {
		case GSP_KEY_Q: *run = false; return false;
		case SDLK_ESCAPE: qs_open = false; *screen = 0; return true;
		case SDLK_UP:
			if (*screen == 0 && la_sel > 0) { la_sel--; return true; }
			if (*screen == 5 && st_sel > 0) { st_sel--; return true; }
			break;
		case SDLK_DOWN:
			if (*screen == 0 && la_sel < LA_N - 1) { la_sel++; return true; }
			if (*screen == 5 && st_sel < 3) { st_sel++; return true; }
			break;
		case SDLK_RETURN:
			if (*screen == 0) { *screen = la_sel + 1; return true; }
			break;
		case SDLK_1: case SDLK_2: case SDLK_3:
		case SDLK_4: case SDLK_5: case SDLK_6:
			qs_open = false;
			*screen = gsp_event_key(ev) - SDLK_1 + 1;
			return true;
		default: break;
		}
		break;
	case GSP_EV_MOUSEDOWN:
		if (ev->button.button == SDL_BUTTON_LEFT) {
			int act = hit_test(ev->button.x / SCALE, ev->button.y / SCALE);
			if (act == A_NONE) return false;
			press_act = act;
			press_live = true;
			press_until = gsp_ticks() + 110;
			return dispatch(act, screen) || true;
		}
		break;
	default: break;
	}
	return false;
}

/* ============================================================
 * SDL
 * ============================================================ */

static int present_frame(SDL_Renderer *ren, SDL_Texture *tex, int screen,
			 uint32_t t, uint8_t bat) {
	render_screen(screen, t, bat);
	/* Brillo = retroiluminacion (PWM), no contenido del panel. Se atenua en
	 * el renderer, despues de expandir a 8 bits: ver backlight_scale(). */
	uint8_t k = backlight_scale();
	gsp_color_mod(tex, k, k, k);
	if (!gsp_update_texture(tex, fb, SCR_W * (int)sizeof(uint16_t)) ||
	    !gsp_clear(ren) || !gsp_copy(ren, tex)) {
		fprintf(stderr, "SDL%d render: %s\n", GSP_SDL_MAJOR, SDL_GetError());
		return -1;
	}
	SDL_RenderPresent(ren);
	return 0;
}

static int smoke_test(void) {
	SDL_Window *win = gsp_window("Gigaspark smoke", WIN_W, WIN_H, true);
	if (!win) return 1;
	SDL_Renderer *ren = gsp_renderer(win, true);
	SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
						  SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H) : NULL;
	int result = tex ? 0 : 1;
	for (int screen = 0; tex && screen < 7; ++screen) {
		result |= present_frame(ren, tex, screen, 1000, 100);
		/* Toda pantalla debe exponer objetivos tactiles de >= 80 px. */
		if (nhits == 0) { fprintf(stderr, "pantalla %d sin zonas tactiles\n", screen); result = 1; }
		for (int i = 0; i < nhits; i++) {
			int m = hits[i].w < hits[i].h ? hits[i].w : hits[i].h;
			if (m < TOUCH_MIN) {
				fprintf(stderr, "pantalla %d: objetivo %d de %d px (< %d)\n",
					screen, hits[i].act, m, TOUCH_MIN);
				result = 1;
			}
		}
	}
	if (tex) SDL_DestroyTexture(tex);
	if (ren) SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	return result ? 1 : 0;
}

int main(int argc, char *argv[]) {
	bool smoke = argc == 2 && strcmp(argv[1], "--smoke") == 0;
	if (argc > 1 && !smoke) {
		fprintf(stderr, "Uso: %s [--smoke]\n", argv[0]);
		return 2;
	}
	if (!gsp_init(SDL_INIT_VIDEO)) {
		fprintf(stderr, "SDL%d: %s\n", GSP_SDL_MAJOR, SDL_GetError());
		return 1;
	}
	if (smoke) {
		int result = smoke_test();
		SDL_Quit();
		if (result == 0) puts("Gigaspark: 7 pantallas OK, objetivos tactiles >= 80 px");
		return result;
	}

	SDL_Window *win = gsp_window("Gigaspark OS - Simulador tactil 800x480",
	                             WIN_W, WIN_H, false);
	if (!win) {
		fprintf(stderr, "SDL%d window: %s\n", GSP_SDL_MAJOR, SDL_GetError());
		SDL_Quit();
		return 1;
	}
	SDL_Renderer *ren = gsp_renderer(win, false);
	if (!ren) ren = gsp_renderer(win, true);
	SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
		SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H) : NULL;
	if (!tex) {
		fprintf(stderr, "SDL%d renderer/texture: %s\n", GSP_SDL_MAJOR, SDL_GetError());
		if (ren) SDL_DestroyRenderer(ren);
		SDL_DestroyWindow(win);
		SDL_Quit();
		return 1;
	}

	bool run = true, dirty = true;
	int screen = 0, result = 0;
	uint8_t bat = 100;
	uint32_t last_battery = gsp_ticks(), last_minute = UINT32_MAX;
	SDL_Event ev;
	while (run) {
		uint32_t frame_start = gsp_ticks();
		if (!screen_is_animated(screen) && !dirty) {
			uint32_t now = gsp_ticks();
			uint32_t elapsed = now - last_battery;
			uint32_t wait = elapsed >= 30000 ? 1 : 30000 - elapsed;
			uint32_t to_minute = 60000 - now % 60000;
			if (to_minute < wait) wait = to_minute;
			if (wf_scan_until && wf_scan_until > now && wf_scan_until - now < wait)
				wait = wf_scan_until - now;
			if (gsp_wait_event(&ev, (int)wait))
				dirty = handle_event(&ev, &screen, &run) || dirty;
		}
		while (gsp_poll_event(&ev))
			dirty = handle_event(&ev, &screen, &run) || dirty;
		if (!run) break;

		uint32_t now = gsp_ticks();
		if (press_live && now >= press_until) { press_live = false; dirty = true; }
		if (wf_scan_until && now >= wf_scan_until) { wf_scan_until = 0; dirty = true; }
		if (now - last_battery >= 30000) {
			if (bat > 5) --bat;
			last_battery = now;
			dirty = true;
		}
		if (now / 60000 != last_minute) { last_minute = now / 60000; dirty = true; }
		if (dirty || screen_is_animated(screen)) {
			if (present_frame(ren, tex, screen, now, bat) < 0) { result = 1; break; }
			dirty = false;
		}
		if (screen_is_animated(screen)) {
			uint32_t elapsed = gsp_ticks() - frame_start;
			if (elapsed < FRAME) SDL_Delay(FRAME - elapsed);
		}
	}
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return result;
}
