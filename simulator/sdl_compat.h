/*
 * Gigaspark OS - Capa de compatibilidad SDL2 / SDL3
 *
 * SDL3 (3.4.x) es la version actual, pero su API rompe con SDL2 en casi todo lo
 * que este simulador usa: crear ventana/renderer, nombres de eventos, tipos de
 * retorno (bool en vez de int) y el reloj (Uint64 en vez de Uint32).
 *
 * Muchos sistemas todavia traen SDL2 como paquete del sistema (Debian/Ubuntu
 * LTS), asi que soportar solo SDL3 dejaria el simulador sin compilar ahi. Esta
 * capa expone una API unica gsp_* y el codigo del simulador no vuelve a
 * mencionar SDL_QUIT vs SDL_EVENT_QUIT ni se preocupa por los tipos.
 *
 * Se elige SDL3 si esta disponible; si no, SDL2. Forzar una:
 *   -DGSP_USE_SDL3=1   o   -DGSP_USE_SDL3=0
 */
#ifndef GIGASPARK_SDL_COMPAT_H
#define GIGASPARK_SDL_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#if !defined(GSP_USE_SDL3)
#  if defined(__has_include)
#    if __has_include(<SDL3/SDL.h>)
#      define GSP_USE_SDL3 1
#    else
#      define GSP_USE_SDL3 0
#    endif
#  else
#    define GSP_USE_SDL3 0
#  endif
#endif

#if GSP_USE_SDL3
#  include <SDL3/SDL.h>
/* SDL_SetMainReady() se declara en SDL_main.h, que SDL.h no incluye.
 * Sin esto la llamada era una declaracion implicita: warning aqui y fallo
 * con -Werror en CI. */
#  include <SDL3/SDL_main.h>
#  define GSP_SDL_MAJOR 3
#else
#  include <SDL.h>
#  define GSP_SDL_MAJOR 2
#endif

/* ---- Nombres de eventos -------------------------------------------------- */
#if GSP_USE_SDL3
#  define GSP_EV_QUIT        SDL_EVENT_QUIT
#  define GSP_EV_KEYDOWN     SDL_EVENT_KEY_DOWN
#  define GSP_EV_MOUSEDOWN   SDL_EVENT_MOUSE_BUTTON_DOWN
#else
#  define GSP_EV_QUIT        SDL_QUIT
#  define GSP_EV_KEYDOWN     SDL_KEYDOWN
#  define GSP_EV_MOUSEDOWN   SDL_MOUSEBUTTONDOWN
#endif

/*
 * SDL2 agrupaba todo lo de ventana en SDL_WINDOWEVENT + ev.window.event.
 * SDL3 emite un evento por caso. Aqui se responde a la pregunta que el
 * simulador realmente hace: "hay que redibujar la ventana?".
 */
static inline bool gsp_window_needs_redraw(const SDL_Event *ev) {
#if GSP_USE_SDL3
	return ev->type == SDL_EVENT_WINDOW_EXPOSED ||
	       ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
	       ev->type == SDL_EVENT_WINDOW_RESTORED;
#else
	return ev->type == SDL_WINDOWEVENT &&
	       (ev->window.event == SDL_WINDOWEVENT_EXPOSED ||
	        ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
	        ev->window.event == SDL_WINDOWEVENT_RESTORED);
#endif
}

/* ---- Teclas -------------------------------------------------------------
 * SDL3 renombro las teclas de letra a mayuscula (SDLK_q -> SDLK_Q) y cambio
 * SDL_KeyboardEvent: el keycode vive en ev->key.key, no en ev->key.keysym.sym.
 * Las demas teclas que usa el simulador (ESCAPE, RETURN, UP, DOWN, 1..6)
 * conservan nombre y valor, comprobado en SDL_keycode.h de 3.4.20. */
#if GSP_USE_SDL3
#  define GSP_KEY_Q SDLK_Q
#else
#  define GSP_KEY_Q SDLK_q
#endif

static inline SDL_Keycode gsp_event_key(const SDL_Event *ev) {
#if GSP_USE_SDL3
	return ev->key.key;
#else
	return ev->key.keysym.sym;
#endif
}

/* Para sintetizar una pulsacion desde el puente. */
static inline void gsp_set_event_key(SDL_Event *ev, SDL_Keycode key) {
#if GSP_USE_SDL3
	ev->key.key = key;
#else
	ev->key.keysym.sym = key;
#endif
}

/* ---- Reloj --------------------------------------------------------------
 * SDL3 devuelve Uint64 (ms). El simulador trabaja en uint32_t: 49.7 dias de
 * wrap, muy por encima de cualquier sesion, y mantiene la aritmetica de plazos
 * ya probada. El reloj del puente y el de la UI DEBEN ser el mismo. */
static inline uint32_t gsp_ticks(void) {
	return (uint32_t)SDL_GetTicks();
}

/* ---- Init / ventana / renderer ------------------------------------------ */
/* SDL3 elimino SDL_INIT_TIMER: SDL_GetTicks() funciona sin subsistema.
 * El puente solo necesita el reloj, no video ni eventos. */
#if GSP_USE_SDL3
#  define GSP_INIT_TIMER 0u
#else
#  define GSP_INIT_TIMER SDL_INIT_TIMER
#endif

static inline bool gsp_init(uint32_t flags) {
#if GSP_USE_SDL3
	return SDL_Init(flags);          /* bool: true = exito */
#else
	return SDL_Init(flags) == 0;     /* int: 0 = exito */
#endif
}

/* SDL3 quita x/y de CreateWindow: se centra solo. */
static inline SDL_Window *gsp_window(const char *title, int w, int h, bool hidden) {
#if GSP_USE_SDL3
	SDL_Window *win = SDL_CreateWindow(title, w, h, hidden ? SDL_WINDOW_HIDDEN : 0);
	return win;
#else
	return SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                        w, h, hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
#endif
}

/* SDL3 selecciona el renderer por nombre, no por indice + flags. */
static inline SDL_Renderer *gsp_renderer(SDL_Window *win, bool software) {
#if GSP_USE_SDL3
	return SDL_CreateRenderer(win, software ? "software" : NULL);
#else
	return SDL_CreateRenderer(win, -1, software ? SDL_RENDERER_SOFTWARE
	                                           : SDL_RENDERER_ACCELERATED);
#endif
}

/* ---- Presentacion ------------------------------------------------------- */
static inline bool gsp_update_texture(SDL_Texture *tex, const void *pixels, int pitch) {
#if GSP_USE_SDL3
	return SDL_UpdateTexture(tex, NULL, pixels, pitch);
#else
	return SDL_UpdateTexture(tex, NULL, pixels, pitch) == 0;
#endif
}

static inline bool gsp_clear(SDL_Renderer *ren) {
#if GSP_USE_SDL3
	return SDL_RenderClear(ren);
#else
	return SDL_RenderClear(ren) == 0;
#endif
}

static inline bool gsp_copy(SDL_Renderer *ren, SDL_Texture *tex) {
#if GSP_USE_SDL3
	return SDL_RenderTexture(ren, tex, NULL, NULL);
#else
	return SDL_RenderCopy(ren, tex, NULL, NULL) == 0;
#endif
}

static inline void gsp_color_mod(SDL_Texture *tex, uint8_t r, uint8_t g, uint8_t b) {
	/* El valor de retorno no aporta nada aqui: si falla, el frame se ve
	 * sin atenuar, no es un error fatal. Se ignora en ambas versiones. */
#if GSP_USE_SDL3
	(void)SDL_SetTextureColorMod(tex, r, g, b);
#else
	(void)SDL_SetTextureColorMod(tex, r, g, b);
#endif
}

/* ---- Espera de eventos --------------------------------------------------
 * SDL2: int (1 evento, 0 timeout, <0 error). SDL3: bool.
 * SDL3 ademas tiene WaitEventTimeoutMS; el de SDL2 ya era en ms. */
static inline bool gsp_wait_event(SDL_Event *ev, int timeout_ms) {
#if GSP_USE_SDL3
	return SDL_WaitEventTimeout(ev, timeout_ms);
#else
	return SDL_WaitEventTimeout(ev, timeout_ms) > 0;
#endif
}

static inline bool gsp_poll_event(SDL_Event *ev) {
#if GSP_USE_SDL3
	return SDL_PollEvent(ev);
#else
	return SDL_PollEvent(ev) != 0;
#endif
}

#endif /* GIGASPARK_SDL_COMPAT_H */
