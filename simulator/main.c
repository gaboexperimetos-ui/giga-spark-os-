/*
 * Gigaspark OS - Simulador tactil
 *
 * Resolucion: 800x480 landscape, la del Arduino GIGA Display Shield real
 * (panel KD040WVFID026 de 3.97", 480x800 nativo, ST7701 por MIPI DSI,
 * tactil Goodix GT911). Antes usaba 480x272 atribuido a un ILI9341, que no
 * corresponde a este hardware. Ver docs/HARDWARE_FIDELITY.md.
 *
 * Diseno tactil: a 0.1081 mm/pixel, un objetivo de 80 px mide 8.6 mm.
 * Ningun boton baja de 80 px de lado menor.
 *
 * Cada boton se dibuja y registra su zona tactil en la misma llamada, asi
 * que lo que se ve es exactamente lo que responde al dedo.
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define SCR_W  800
#define SCR_H  480
#define SCALE  1
#define WIN_W  (SCR_W * SCALE)
#define WIN_H  (SCR_H * SCALE)
#define FPS    30
#define FRAME  (1000 / FPS)

/* Cromo comun */
#define BAR_H   56
#define RAIL_W  96
#define CX      RAIL_W
#define CY      BAR_H
#define CW      (SCR_W - RAIL_W)
#define CH      (SCR_H - BAR_H)
#define PAD     16
#define GAP     12
#define TOUCH_MIN 80

/* Paleta RGB565 (el formato real del panel) */
#define BLK  0x0000
#define WHT  0xFFFF
#define BG   0x0864
#define PANEL 0x10A5
#define CRD  0x1927
#define CRD2 0x21AA
#define BDR  0x29A9
#define ACC  0x2EFF
#define ACCD 0x0BB2
#define MUT  0x9D77
#define LGR  0xC677
#define GRY  0x7BEF
#define GRN  0x3E73
#define GRND 0x0BEF
#define RED  0xFB0E
#define REDD 0x8106
#define YEL  0xFDE4
#define ORG  0xFD20
#define PRP  0x9A7F
#define CYN  0x2EFF
#define BLU  0x2A5F

static uint16_t fb[SCR_W * SCR_H];

static void px(int x, int y, uint16_t c) {
	if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H)
		fb[y * SCR_W + x] = c;
}

static void frect(int x, int y, int w, int h, uint16_t c) {
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	int x1 = x + w > SCR_W ? SCR_W : x + w;
	int y1 = y + h > SCR_H ? SCR_H : y + h;
	for (int j = y0; j < y1; j++) {
		uint16_t *row = fb + (size_t)j * SCR_W;
		for (int i = x0; i < x1; i++) row[i] = c;
	}
}

static void drect(int x, int y, int w, int h, uint16_t c) {
	frect(x, y, w, 1, c);
	frect(x, y + h - 1, w, 1, c);
	frect(x, y, 1, h, c);
	frect(x + w - 1, y, 1, h, c);
}

/* Rectangulo redondeado por scanline, sin aliasing costoso. */
static void rrect(int x, int y, int w, int h, int r, uint16_t c) {
	if (w <= 0 || h <= 0) return;
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	for (int j = 0; j < h; j++) {
		int dy = 0;
		if (j < r) dy = r - j;
		else if (j >= h - r) dy = j - (h - r) + 1;
		int inset = 0;
		if (dy > 0) {
			int dd = r * r - dy * dy;
			inset = dd <= 0 ? r : r - (int)(sqrtf((float)dd) + 0.5f);
		}
		frect(x + inset, y + j, w - 2 * inset, 1, c);
	}
}

static void card(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border) {
	rrect(x, y, w, h, r, border);
	rrect(x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, fill);
}

/* ============================================================
 * FUENTE 8x16
 * ============================================================ */

static const uint8_t FNT[][16] = {
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x18,0x3C,0x3C,0x3C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
	{0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x00,0x00,0x00,0x00},
	{0x18,0x18,0x7C,0xC6,0xC2,0xC0,0x7C,0x06,0x06,0x86,0xC6,0x7C,0x18,0x18,0x00,0x00},
	{0x00,0x00,0x00,0x00,0xC2,0xC6,0x0C,0x18,0x30,0x60,0xC6,0x86,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	{0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0xC6,0xCE,0xDE,0xF6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC0,0xC6,0xFE,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xFE,0xC6,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xDC,0xC0,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xE6,0x66,0x66,0x6C,0x78,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xF0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x0C,0x0E,0x00,0x00},
	{0x00,0x00,0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7C,0xC6,0xC6,0x60,0x38,0x0C,0x06,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0xEE,0x6C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xC6,0xC6,0x6C,0x7C,0x38,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC2,0xC6,0xFE,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x80,0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00},
	{0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00},
	{0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	{0x00,0x00,0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x1C,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00},
	{0x00,0x00,0xE0,0x60,0x60,0x6C,0x76,0x66,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00},
	{0x00,0x00,0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0x66,0xE6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x10,0x30,0x30,0xFC,0x30,0x30,0x30,0x30,0x36,0x1C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x10,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0xF8,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0xFE,0xCC,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* El panel no tiene fuente vectorial: se transliteran los acentos UTF-8
 * a ASCII para que el texto en espanol se lea completo. */
static char fold_char(const char **p) {
	const unsigned char *s = (const unsigned char *)*p;
	if (s[0] < 0x80) { (*p)++; return (char)s[0]; }
	if (s[0] == 0xC3 && s[1]) {
		unsigned char c = s[1];
		*p += 2;
		switch (c) {
		case 0xA1: return 'a'; case 0x81: return 'A';
		case 0xA9: return 'e'; case 0x89: return 'E';
		case 0xAD: return 'i'; case 0x8D: return 'I';
		case 0xB3: return 'o'; case 0x93: return 'O';
		case 0xBA: return 'u'; case 0x9A: return 'U';
		case 0xB1: return 'n'; case 0x91: return 'N';
		case 0xBC: return 'u'; case 0x9C: return 'U';
		default:   return '?';
		}
	}
	(*p)++;
	return '?';
}

static void fchar(int x, int y, char c, uint16_t col, int sc) {
	int i = c - 32;
	if (i < 0 || i > 94) return;
	const uint8_t *g = FNT[i];
	for (int r = 0; r < 16; r++) {
		uint8_t b = g[r];
		if (!b) continue;
		for (int c2 = 0; c2 < 8; c2++) {
			if (!(b & (0x80 >> c2))) continue;
			if (sc == 1) px(x + c2, y + r, col);
			else frect(x + c2 * sc, y + r * sc, sc, sc, col);
		}
	}
}

static int text_w(const char *s, int sc) {
	int n = 0;
	while (*s) { fold_char(&s); n++; }
	return n * 8 * sc;
}

static void fstr(int x, int y, const char *s, uint16_t col, int sc) {
	while (*s) {
		char c = fold_char(&s);
		fchar(x, y, c, col, sc);
		x += 8 * sc;
	}
}

static void fstrc(int cx, int y, const char *s, uint16_t col, int sc) {
	fstr(cx - text_w(s, sc) / 2, y, s, col, sc);
}

/* Texto centrado vertical y horizontalmente dentro de una caja. */
static void fbox(int x, int y, int w, int h, const char *s, uint16_t col, int sc) {
	fstr(x + (w - text_w(s, sc)) / 2, y + (h - 16 * sc) / 2, s, col, sc);
}

static void utoa(uint32_t v, char *b) {
	char t[12]; int i = 0;
	if (!v) { b[0] = '0'; b[1] = 0; return; }
	while (v) { t[i++] = '0' + v % 10; v /= 10; }
	for (int j = 0; j < i; j++) b[j] = t[i - 1 - j];
	b[i] = 0;
}

/* ============================================================
 * REGISTRO TACTIL
 * Dibujar un boton registra su zona. No hay coordenadas duplicadas.
 * ============================================================ */

/* Acciones simples: valores consecutivos bajos.
 * Grupos indexados (base + indice): cada uno con su rango reservado, para que
 * base+indice nunca caiga dentro de otra accion. Antes se solapaban y tocar
 * una red WiFi bajaba el brillo. Los _Static_assert de abajo lo impiden. */
enum {
	A_NONE = 0,
	A_BACK, A_HOME, A_MENU, A_QS_CLOSE,
	A_SC_RUN, A_SC_TB_M, A_SC_TB_P, A_SC_AUTO, A_SC_CH1, A_SC_CH2,
	A_PS_V_M, A_PS_V_P, A_PS_I_M, A_PS_I_P, A_PS_OUT,
	A_RF_SCAN, A_WF_SCAN, A_WF_PREV, A_WF_NEXT,
	A_ST_WIFI, A_ST_SOUND, A_ST_BR_M, A_ST_BR_P, A_ST_SLEEP,
	A_APP     = 100,   /* 100..105  abrir app */
	A_RF_MODE = 120,   /* 120..123  modo RF */
	A_RF_FREQ = 140,   /* 140..143  preset de frecuencia */
	A_WF_NET  = 160    /* 160..164  red WiFi */
};

#define N_APPS      6
#define N_RF_MODES  4
#define N_RF_FREQS  4
#define N_WF_NETS   5

_Static_assert(A_ST_SLEEP < A_APP, "grupos indexados pisan acciones simples");
_Static_assert(A_APP + N_APPS <= A_RF_MODE, "grupo apps pisa modos RF");
_Static_assert(A_RF_MODE + N_RF_MODES <= A_RF_FREQ, "modos RF pisan presets");
_Static_assert(A_RF_FREQ + N_RF_FREQS <= A_WF_NET, "presets pisan redes WiFi");
_Static_assert(A_WF_NET + N_WF_NETS <= 32767, "acciones fuera de rango");

typedef struct { int16_t x, y, w, h; int act; } Hit;
#define MAX_HITS 80
static Hit hits[MAX_HITS];
static int nhits;
static int press_act = -1;
static bool press_live;

static void hit_add(int x, int y, int w, int h, int act) {
	if (nhits >= MAX_HITS || act == A_NONE) return;
	hits[nhits].x = (int16_t)x; hits[nhits].y = (int16_t)y;
	hits[nhits].w = (int16_t)w; hits[nhits].h = (int16_t)h;
	hits[nhits].act = act;
	nhits++;
}

static int hit_test(int x, int y) {
	for (int i = nhits - 1; i >= 0; i--) {
		const Hit *b = &hits[i];
		if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h)
			return b->act;
	}
	return A_NONE;
}

/* ============================================================
 * WIDGETS TACTILES
 * ============================================================ */

enum { BS_NORMAL, BS_ACCENT, BS_ON, BS_DANGER, BS_SEL, BS_OFF };

static void btn(int x, int y, int w, int h, const char *t, int style, int act) {
	uint16_t fill = CRD, edge = BDR, fg = WHT;
	if (style == BS_ACCENT) { fill = ACCD; edge = ACC; }
	else if (style == BS_ON) { fill = GRND; edge = GRN; }
	else if (style == BS_DANGER) { fill = REDD; edge = RED; }
	else if (style == BS_SEL) { fill = ACCD; edge = ACC; }
	else if (style == BS_OFF) { fill = BG; edge = CRD; fg = BDR; }
	bool down = press_live && act != A_NONE && act == press_act;
	if (down) { fill = ACCD; edge = WHT; }
	card(x, y, w, h, 14, fill, edge);
	int sc = text_w(t, 2) <= w - 16 ? 2 : 1;
	fbox(x, y, w, h, t, fg, sc);
	hit_add(x, y, w, h, act);
}

static void wbar(int x, int y, int w, int h, int pct, uint16_t fg) {
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	rrect(x, y, w, h, h / 2, BDR);
	int fw = (w - 4) * pct / 100;
	if (fw > 2) rrect(x + 2, y + 2, fw, h - 4, (h - 4) / 2, fg);
}

static void wswitch(int x, int y, bool on, int act) {
	int w = 112, h = 64;
	rrect(x, y, w, h, h / 2, on ? ACCD : BDR);
	int kx = on ? x + w - 56 : x + 8;
	rrect(kx, y + 8, 48, 48, 24, on ? WHT : MUT);
	hit_add(x - 8, y - 8, w + 16, h + 16, act);
}

static void wbatt(int x, int y, uint8_t pct) {
	drect(x, y, 44, 22, LGR);
	frect(x + 44, y + 6, 4, 10, LGR);
	int fw = 38 * pct / 100;
	if (fw > 0) frect(x + 3, y + 3, fw, 16, pct > 20 ? GRN : RED);
	char b[16]; utoa(pct, b);
	strcat(b, "%");
	fstr(x - text_w(b, 2) - 10, y + 3, b, MUT, 2);
}

/* Iconos vectoriales simples, dibujados con rectangulos y lineas. */
static void icon(int cx, int cy, int kind, uint16_t col) {
	if (kind == 0) {                    /* onda */
		for (int i = -28; i <= 28; i++) {
			int y = (int)(sinf((float)i * 0.16f) * 20.0f);
			frect(cx + i, cy + y, 2, 3, col);
		}
	} else if (kind == 1) {             /* rayo */
		for (int j = -26; j < 0; j++) {
			int w = 20 - (j + 26) * 14 / 26;
			frect(cx - w / 2 + 6, cy + j, w > 3 ? w : 3, 1, col);
		}
		for (int j = 0; j < 26; j++) {
			int w = 20 - j * 14 / 26;
			frect(cx - w / 2 - 6, cy + j, w > 3 ? w : 3, 1, col);
		}
		frect(cx - 16, cy - 2, 32, 4, col);
	} else if (kind == 2) {             /* RF */
		rrect(cx - 4, cy - 4, 8, 8, 4, col);
		for (int k = 1; k <= 3; k++) {
			int r = 8 + k * 8;
			for (int a = -60; a <= 60; a += 3) {
				float rad = (float)a * 0.01745f;
				px(cx + (int)(sinf(rad) * r), cy - (int)(cosf(rad) * r), col);
				px(cx - (int)(sinf(rad) * r), cy - (int)(cosf(rad) * r), col);
			}
		}
	} else if (kind == 3) {             /* wifi */
		for (int k = 1; k <= 3; k++) {
			int r = k * 11;
			for (int a = -50; a <= 50; a += 2) {
				float rad = (float)a * 0.01745f;
				int xx = cx + (int)(sinf(rad) * r);
				int yy = cy + 16 - (int)(cosf(rad) * r);
				frect(xx, yy, 2, 3, col);
			}
		}
		rrect(cx - 3, cy + 12, 7, 7, 3, col);
	} else if (kind == 4) {             /* ajustes */
		for (int k = 0; k < 3; k++) {
			int y = cy - 18 + k * 18;
			frect(cx - 26, y, 52, 3, col);
			int kx = cx + (k == 0 ? -12 : k == 1 ? 10 : -4);
			rrect(kx - 6, y - 5, 13, 13, 6, col);
		}
	} else {                            /* info */
		for (int a = 0; a < 360; a += 2) {
			float rad = (float)a * 0.01745f;
			px(cx + (int)(cosf(rad) * 26), cy + (int)(sinf(rad) * 26), col);
			px(cx + (int)(cosf(rad) * 25), cy + (int)(sinf(rad) * 25), col);
		}
		frect(cx - 2, cy - 12, 5, 5, col);
		frect(cx - 2, cy - 2, 5, 16, col);
	}
}

/* ============================================================
 * CROMO: barra de estado + riel lateral
 * ============================================================ */

static const char *app_names[] = {
	"OSCILOSCOPIO", "FUENTE", "FLIPPER RF", "WIFI", "AJUSTES", "ACERCA DE"
};

static void chrome(uint32_t t, uint8_t bat, int screen) {
	frect(0, 0, SCR_W, BAR_H, PANEL);
	frect(0, BAR_H - 2, SCR_W, 2, BDR);

	uint32_t s = t / 1000;
	char tb[8];
	tb[0] = '0' + (s / 3600) % 24 / 10; tb[1] = '0' + (s / 3600) % 24 % 10;
	tb[2] = ':'; tb[3] = '0' + (s / 60) % 60 / 10; tb[4] = '0' + (s / 60) % 10;
	tb[5] = 0;
	fstr(PAD, (BAR_H - 32) / 2, tb, WHT, 2);

	static const uint16_t accents[] = { ACC, GRN, ORG, PRP, ACC, ACC, ACC };
	const char *title = screen == 0 ? "GIGASPARK OS" : app_names[screen - 1];
	fstrc(SCR_W / 2, (BAR_H - 32) / 2, title, accents[screen % 7], 2);

	wbatt(SCR_W - 60, (BAR_H - 22) / 2, bat);

	/* Riel: tres objetivos grandes, alcanzables con el pulgar. */
	frect(0, BAR_H, RAIL_W, CH, PANEL);
	frect(RAIL_W - 2, BAR_H, 2, CH, BDR);
	int bh = CH / 3;
	for (int i = 0; i < 3; i++) {
		int y = BAR_H + i * bh;
		int act = i == 0 ? A_BACK : i == 1 ? A_HOME : A_MENU;
		bool down = press_live && press_act == act;
		bool disabled = i == 0 && screen == 0;
		if (down) frect(0, y, RAIL_W - 2, bh, CRD2);
		uint16_t col = disabled ? BDR : (i == 1 ? ACC : MUT);
		int mx = RAIL_W / 2, my = y + bh / 2;
		if (i == 0) {
			for (int k = 0; k < 18; k++) {
				frect(mx - 10 + k, my - k, 3, 3, col);
				frect(mx - 10 + k, my + k, 3, 3, col);
			}
		} else if (i == 1) {
			for (int a = 0; a < 2; a++)
				for (int b = 0; b < 2; b++)
					rrect(mx - 22 + a * 24, my - 22 + b * 24, 20, 20, 4, col);
		} else {
			for (int k = 0; k < 3; k++)
				rrect(mx - 22, my - 18 + k * 14, 44, 6, 3, col);
		}
		if (!disabled) hit_add(0, y, RAIL_W, bh, act);
		if (i < 2) frect(0, y + bh - 2, RAIL_W - 2, 2, CRD);
	}
}

/* ============================================================
 * LAUNCHER
 * ============================================================ */

static int la_sel;
#define LA_N N_APPS
static const char *la_nm[] = {
	"Osciloscopio", "Fuente", "Flipper RF", "WiFi", "Ajustes", "Acerca de"
};

static void app_launch(uint32_t t, uint8_t bat) {
	chrome(t, bat, 0);
	int cols = 3, rows = 2;
	int tw = (CW - 2 * PAD - (cols - 1) * GAP) / cols;
	int th = (CH - 2 * PAD - (rows - 1) * GAP) / rows;
	for (int i = 0; i < LA_N; i++) {
		int c = i % cols, r = i / cols;
		int x = CX + PAD + c * (tw + GAP);
		int y = CY + PAD + r * (th + GAP);
		bool sel = i == la_sel;
		bool down = press_live && press_act == A_APP + i;
		card(x, y, tw, th, 16, down ? ACCD : (sel ? CRD2 : CRD),
		     sel || down ? ACC : BDR);
		icon(x + tw / 2, y + th / 2 - 18, i, down ? WHT : ACC);
		fstrc(x + tw / 2, y + th - 44, la_nm[i], sel ? WHT : LGR, 2);
		hit_add(x, y, tw, th, A_APP + i);
	}
}

/* ============================================================
 * OSCILOSCOPIO
 * ============================================================ */

static float sc_phase;
static bool sc_run = true, sc_ch1 = true, sc_ch2 = true;
static int sc_tb = 3;
static const char *tb_lbl[] = {
	"100 us", "200 us", "500 us", "1 ms", "2 ms", "5 ms", "10 ms"
};

static void app_scope(uint32_t t, uint8_t bat) {
	chrome(t, bat, 1);
	int by = CY;
	int bh = CY + CH - by;
	int ctrl_h = 88;
	int gx = CX + PAD, gy = by + PAD;
	int gw = CW - 2 * PAD, gh = bh - 2 * PAD - ctrl_h - GAP;

	card(gx, gy, gw, gh, 12, 0x0825, BDR);
	for (int i = 1; i < 10; i++) {
		int x = gx + i * gw / 10;
		for (int y = gy + 4; y < gy + gh - 4; y += 4) px(x, y, 0x18E6);
	}
	for (int i = 1; i < 4; i++) {
		int y = gy + i * gh / 4;
		for (int x = gx + 4; x < gx + gw - 4; x += 4) px(x, y, 0x18E6);
	}
	for (int x = gx + 4; x < gx + gw - 4; x += 2) px(x, gy + gh / 2, 0x2949);

	if (sc_run) sc_phase += 0.09f;
	float cycles = 2.0f + (float)sc_tb * 0.7f;
	if (sc_ch1) {
		for (int x = 4; x < gw - 4; x++) {
			float a = (float)x / (float)gw * cycles * 6.28318f + sc_phase;
			int y = gy + gh / 2 - (int)(sinf(a) * (float)(gh / 2 - 16));
			frect(gx + x, y, 1, 3, GRN);
		}
	}
	if (sc_ch2) {
		int prev = 0;
		for (int x = 4; x < gw - 4; x++) {
			float a = (float)x / (float)gw * cycles * 3.14159f + sc_phase * 0.7f;
			int y = gy + gh / 2 + (sinf(a) > 0 ? -(gh / 5) : (gh / 5));
			frect(gx + x, y, 1, 2, YEL);
			if (x > 4 && y != prev) {
				int lo = y < prev ? y : prev, hi = y < prev ? prev : y;
				frect(gx + x, lo, 2, hi - lo + 2, YEL);
			}
			prev = y;
		}
	}
	fstr(gx + 12, gy + 10, "CH1", sc_ch1 ? GRN : BDR, 1);
	fstr(gx + 60, gy + 10, "CH2", sc_ch2 ? YEL : BDR, 1);
	char tl[24]; strcpy(tl, tb_lbl[sc_tb]); strcat(tl, "/div");
	fstr(gx + gw - text_w(tl, 1) - 12, gy + 10, tl, MUT, 1);

	int cy2 = gy + gh + GAP;
	int n = 6, bw = (gw - (n - 1) * GAP) / n;
	btn(gx + 0 * (bw + GAP), cy2, bw, ctrl_h, "-", BS_NORMAL, A_SC_TB_M);
	btn(gx + 1 * (bw + GAP), cy2, bw, ctrl_h, "+", BS_NORMAL, A_SC_TB_P);
	btn(gx + 2 * (bw + GAP), cy2, bw, ctrl_h, "AUTO", BS_ACCENT, A_SC_AUTO);
	btn(gx + 3 * (bw + GAP), cy2, bw, ctrl_h, "CH1", sc_ch1 ? BS_ON : BS_NORMAL, A_SC_CH1);
	btn(gx + 4 * (bw + GAP), cy2, bw, ctrl_h, "CH2", sc_ch2 ? BS_ON : BS_NORMAL, A_SC_CH2);
	btn(gx + 5 * (bw + GAP), cy2, bw, ctrl_h, sc_run ? "PAUSA" : "SEGUIR",
	    sc_run ? BS_ACCENT : BS_ON, A_SC_RUN);
}

/* ============================================================
 * FUENTE DE PODER
 * ============================================================ */

static int ps_v = 50, ps_i = 10;   /* decimas: 5.0 V, 1.0 A */
static bool ps_out;

static void fmt_tenth(int v, char *b) {
	char n[12]; utoa((uint32_t)(v / 10), n);
	strcpy(b, n);
	size_t l = strlen(b);
	b[l] = '.'; b[l + 1] = '0' + v % 10; b[l + 2] = 0;
}

static void app_psu(uint32_t t, uint8_t bat) {
	chrome(t, bat, 2);
	int by = CY;
	int bh = CY + CH - by;
	int out_h = 88;
	int cardh = bh - 2 * PAD - out_h - GAP;
	int cw = (CW - 2 * PAD - GAP) / 2;

	for (int k = 0; k < 2; k++) {
		int x = CX + PAD + k * (cw + GAP), y = by + PAD;
		uint16_t ac = k == 0 ? GRN : YEL;
		card(x, y, cw, cardh, 14, PANEL, BDR);
		fstrc(x + cw / 2, y + 14, k == 0 ? "VOLTAJE" : "CORRIENTE MAX", MUT, 1);
		char v[24]; fmt_tenth(k == 0 ? ps_v : ps_i, v);
		strcat(v, k == 0 ? " V" : " A");
		fstrc(x + cw / 2, y + 52, v, ac, 4);
		int sh = 80, sw = (cw - 3 * GAP) / 2;
		int sy = y + cardh - sh - PAD;
		wbar(x + 24, sy - 62, cw - 48, 16,
		     k == 0 ? ps_v * 100 / 240 : ps_i * 100 / 50, ac);
		fstrc(x + cw / 2, sy - 34, k == 0 ? "0 - 24.0 V" : "0 - 5.0 A", MUT, 1);
		btn(x + GAP, sy, sw, sh, "-", BS_NORMAL, k == 0 ? A_PS_V_M : A_PS_I_M);
		btn(x + 2 * GAP + sw, sy, sw, sh, "+", BS_NORMAL, k == 0 ? A_PS_V_P : A_PS_I_P);
	}

	int oy = by + PAD + cardh + GAP;
	btn(CX + PAD, oy, CW - 2 * PAD, out_h,
	    ps_out ? "APAGAR SALIDA" : "ENCENDER SALIDA",
	    ps_out ? BS_ON : BS_DANGER, A_PS_OUT);
}

/* ============================================================
 * FLIPPER RF
 * ============================================================ */

static int fl_mode;
static const char *fl_modes[] = { "SUB-GHz", "NFC/RFID", "IR", "GPIO" };
static int rf_freq = 1;
static const char *rf_lbl[] = { "315", "433.92", "868.35", "915" };
static bool rf_scan;

static void app_flip(uint32_t t, uint8_t bat) {
	chrome(t, bat, 3);
	int by = CY;
	int rowh = 80, freqh = 100;
	int y = by + PAD;
	int iw = CW - 2 * PAD;

	card(CX + PAD, y, iw, freqh, 14, PANEL, BDR);
	char f[24]; strcpy(f, rf_lbl[rf_freq]); strcat(f, " MHz");
	fstr(CX + PAD + 20, y + (freqh - 32) / 2, f, ACC, 2);
	int bars = rf_scan ? (int)(t / 120) % 11 : 3 + (int)(t / 400) % 4;
	for (int i = 0; i < 10; i++) {
		int bx = CX + PAD + iw - 24 - (10 - i) * 16;
		int h = 12 + i * 5;
		frect(bx, y + freqh - 20 - h, 11, h, i < bars ? GRN : BDR);
	}
	fstr(CX + PAD + 20, y + freqh - 24, fl_modes[fl_mode], MUT, 1);
	y += freqh + GAP;

	int mw = (iw - 3 * GAP) / 4;
	for (int i = 0; i < 4; i++)
		btn(CX + PAD + i * (mw + GAP), y, mw, rowh, fl_modes[i],
		    i == fl_mode ? BS_SEL : BS_NORMAL, A_RF_MODE + i);
	y += rowh + GAP;
	for (int i = 0; i < 4; i++)
		btn(CX + PAD + i * (mw + GAP), y, mw, rowh, rf_lbl[i],
		    i == rf_freq ? BS_SEL : BS_NORMAL, A_RF_FREQ + i);
	y += rowh + GAP;
	btn(CX + PAD, y, iw, rowh, rf_scan ? "DETENER" : "ESCANEAR",
	    rf_scan ? BS_ON : BS_ACCENT, A_RF_SCAN);
}

/* ============================================================
 * WIFI
 * ============================================================ */

static const char *wf_nm[] = {
	"Gigaspark_AP", "MiFibra-5G", "MOVISTAR_7F2E", "JAZZTEL_8FA2", "WIFI_GUEST"
};
static const int wf_sig[] = { 95, 85, 72, 55, 40 };
static int wf_conn;
static bool wf_on = true;
static uint32_t wf_scan_until;
static int wf_page;

static void app_wifi(uint32_t t, uint8_t bat) {
	chrome(t, bat, 4);
	int by = CY;
	int iw = CW - 2 * PAD;
	int y = by + PAD;
	int th = 80, sq = 80, pgw = 72;
	int bw = iw - 2 * sq - pgw - 3 * GAP;
	int pages = (5 + 2) / 3;

	btn(CX + PAD, y, sq, th, "<", wf_page > 0 ? BS_NORMAL : BS_OFF,
	    wf_page > 0 ? A_WF_PREV : A_NONE);
	btn(CX + PAD + sq + GAP, y, sq, th, ">",
	    wf_page < pages - 1 ? BS_NORMAL : BS_OFF,
	    wf_page < pages - 1 ? A_WF_NEXT : A_NONE);
	char pg[16]; utoa((uint32_t)(wf_page + 1), pg);
	strcat(pg, "/"); { char n[8]; utoa((uint32_t)pages, n); strcat(pg, n); }
	fbox(CX + PAD + 2 * (sq + GAP), y, pgw, th, pg, MUT, 1);
	btn(CX + PAD + 2 * sq + pgw + 3 * GAP, y, bw, th,
	    t < wf_scan_until ? "BUSCANDO..." : "REESCANEAR", BS_ACCENT, A_WF_SCAN);
	y += th + GAP;

	if (!wf_on) {
		card(CX + PAD, y, iw, 88, 14, CRD, BDR);
		fbox(CX + PAD, y, iw, 88, "WiFi apagado - activalo en Ajustes", MUT, 1);
		return;
	}
	int rowh = 88;
	for (int k = 0; k < 3; k++) {
		int i = wf_page * 3 + k;
		if (i >= 5) break;
		int x = CX + PAD;
		bool down = press_live && press_act == A_WF_NET + i;
		card(x, y, iw, rowh, 14, down ? CRD2 : CRD, i == wf_conn ? GRN : BDR);
		fstr(x + 20, y + (rowh - 32) / 2, wf_nm[i], WHT, 2);
		char s[16]; utoa((uint32_t)wf_sig[i], s); strcat(s, "%");
		fstr(x + iw - 150, y + (rowh - 16) / 2, s, MUT, 1);
		if (i == wf_conn) fstr(x + iw - 70, y + (rowh - 16) / 2, "OK", GRN, 1);
		wbar(x + iw - 250, y + rowh / 2 - 6, 80, 12, wf_sig[i], wf_sig[i] > 60 ? GRN : YEL);
		hit_add(x, y, iw, rowh, A_WF_NET + i);
		y += rowh + GAP;
	}
}

/* ============================================================
 * AJUSTES
 * ============================================================ */

static bool st_sound = true;
static int st_br = 85;
static int st_sleep = 1;
static const int st_sleeps[] = { 15, 30, 60, 0 };
static int st_sel;

static void app_set(uint32_t t, uint8_t bat) {
	chrome(t, bat, 5);
	int by = CY;
	int rowh = 88;
	int y = by + PAD;
	int w = CW - 2 * PAD;

	for (int i = 0; i < 4; i++) {
		bool sel = i == st_sel;
		card(CX + PAD, y, w, rowh, 14, CRD, sel ? ACC : BDR);
		if (i == 0) {
			fstr(CX + PAD + 20, y + (rowh - 32) / 2, "WiFi", WHT, 2);
			wswitch(CX + PAD + w - 132, y + (rowh - 64) / 2, wf_on, A_ST_WIFI);
		} else if (i == 1) {
			fstr(CX + PAD + 20, y + (rowh - 32) / 2, "Brillo", WHT, 2);
			int bw = 200;
			wbar(CX + PAD + 180, y + rowh / 2 - 8, bw, 16, st_br, ACC);
			char b[16]; utoa((uint32_t)st_br, b); strcat(b, "%");
			fstr(CX + PAD + 400, y + (rowh - 16) / 2, b, MUT, 1);
			btn(CX + PAD + w - 184, y + 4, 80, rowh - 8, "-", BS_NORMAL, A_ST_BR_M);
			btn(CX + PAD + w - 92, y + 4, 80, rowh - 8, "+", BS_NORMAL, A_ST_BR_P);
		} else if (i == 2) {
			fstr(CX + PAD + 20, y + (rowh - 32) / 2, "Sonido tactil", WHT, 2);
			wswitch(CX + PAD + w - 132, y + (rowh - 64) / 2, st_sound, A_ST_SOUND);
		} else {
			fstr(CX + PAD + 20, y + (rowh - 32) / 2, "Suspension", WHT, 2);
			char b[16];
			if (st_sleeps[st_sleep] == 0) strcpy(b, "Nunca");
			else { utoa((uint32_t)st_sleeps[st_sleep], b); strcat(b, " s"); }
			btn(CX + PAD + w - 200, y + 4, 188, rowh - 8, b, BS_NORMAL, A_ST_SLEEP);
		}
		y += rowh + GAP;
	}
}

/* ============================================================
 * ACERCA DE
 * ============================================================ */

static void app_about(uint32_t t, uint8_t bat) {
	chrome(t, bat, 6);
	int by = CY;
	static const char *k[] = {
		"Equipo", "MCU", "Panel", "Tactil", "Sistema", "Memoria"
	};
	static const char *v[] = {
		"Arduino GIGA R1 + Display Shield",
		"STM32H747XI  M7 480 + M4 240 MHz",
		"480x800 nativo  ST7701 MIPI DSI",
		"Goodix GT911  I2C  5 puntos",
		"Gigaspark OS  Zephyr RTOS",
		"Buddy + zRAM"
	};
	int rowh = (SCR_H - PAD - (by + PAD) - 5 * 6) / 6;
	for (int i = 0; i < 6; i++) {
		int y = by + PAD + i * (rowh + 6);
		if (y + rowh > SCR_H - PAD) break;
		card(CX + PAD, y, CW - 2 * PAD, rowh, 10, CRD, BDR);
		fstr(CX + PAD + 16, y + (rowh - 16) / 2, k[i], WHT, 1);
		fstr(CX + PAD + 160, y + (rowh - 16) / 2, v[i], MUT, 1);
	}
}

/* ============================================================
 * AJUSTES RAPIDOS (panel superpuesto)
 * ============================================================ */

static bool qs_open;

static void draw_qs(uint32_t t, uint8_t bat) {
	int w = 360, x = SCR_W - w;
	frect(CX, CY, SCR_W - CX, SCR_H - CY, 0x0000);
	frect(x, 0, w, SCR_H, PANEL);
	frect(x, 0, 2, SCR_H, BDR);
	nhits = 0;
	hit_add(CX, CY, x - CX, SCR_H - CY, A_QS_CLOSE);
	fstr(x + PAD, 20, "AJUSTES RAPIDOS", MUT, 1);
	btn(x + w - 96, 8, 80, 80, "X", BS_NORMAL, A_QS_CLOSE);

	int y = 110;
	fstr(x + PAD, y, "Brillo", WHT, 2); y += 40;
	wbar(x + PAD, y, w - 2 * PAD - 184, 16, st_br, ACC);
	btn(x + w - PAD - 176, y - 32, 80, 80, "-", BS_NORMAL, A_ST_BR_M);
	btn(x + w - PAD - 84, y - 32, 80, 80, "+", BS_NORMAL, A_ST_BR_P);
	y += 70;
	fstr(x + PAD, y + 16, "WiFi", WHT, 2);
	wswitch(x + w - 132, y, wf_on, A_ST_WIFI);
	y += 90;
	char b[16]; utoa(bat, b); strcat(b, "% bateria");
	fstr(x + PAD, y, b, MUT, 1);
	y += 40;
	btn(x + PAD, y, w - 2 * PAD, 88, "ABRIR AJUSTES", BS_ACCENT, A_APP + 4);
	(void)t;
}

/* ============================================================
 * RENDER
 * ============================================================ */

static bool screen_is_animated(int screen) {
	return screen == 1 || screen == 3 || press_live;
}

static void render_screen(int screen, uint32_t t, uint8_t bat) {
	memset(fb, 0, sizeof(fb));
	nhits = 0;
	switch (screen) {
	case 1: app_scope(t, bat); break;
	case 2: app_psu(t, bat); break;
	case 3: app_flip(t, bat); break;
	case 4: app_wifi(t, bat); break;
	case 5: app_set(t, bat); break;
	case 6: app_about(t, bat); break;
	default: app_launch(t, bat); break;
	}
	if (qs_open) draw_qs(t, bat);

	/* Brillo: escalado en RGB565 sin sacar el pixel del formato del panel. */
	if (st_br < 100) {
		int k = 40 + st_br * 215 / 100;
		for (size_t i = 0; i < sizeof(fb) / sizeof(fb[0]); i++) {
			uint16_t c = fb[i];
			uint16_t r = (uint16_t)(((c >> 11) & 31) * k / 255);
			uint16_t g = (uint16_t)(((c >> 5) & 63) * k / 255);
			uint16_t b = (uint16_t)((c & 31) * k / 255);
			fb[i] = (uint16_t)((r << 11) | (g << 5) | b);
		}
	}
}

/* ============================================================
 * ENTRADA
 * ============================================================ */

static bool dispatch(int act, int *screen) {
	if (act == A_NONE) return false;
	if (act == A_QS_CLOSE) { qs_open = false; return true; }
	if (act == A_BACK) {
		if (qs_open) qs_open = false;
		else *screen = 0;
		return true;
	}
	if (act == A_HOME) { qs_open = false; *screen = 0; return true; }
	if (act == A_MENU) { qs_open = !qs_open; return true; }
	if (act >= A_APP && act < A_APP + N_APPS) {
		qs_open = false;
		*screen = act - A_APP + 1;
		la_sel = act - A_APP;
		return true;
	}
	switch (act) {
	case A_SC_RUN: sc_run = !sc_run; return true;
	case A_SC_TB_M: if (sc_tb > 0) sc_tb--; return true;
	case A_SC_TB_P: if (sc_tb < 6) sc_tb++; return true;
	case A_SC_AUTO: sc_tb = 3; sc_run = true; sc_ch1 = sc_ch2 = true; return true;
	case A_SC_CH1: sc_ch1 = !sc_ch1; return true;
	case A_SC_CH2: sc_ch2 = !sc_ch2; return true;
	case A_PS_V_M: if (ps_v > 0) ps_v -= 5; return true;
	case A_PS_V_P: if (ps_v < 240) ps_v += 5; return true;
	case A_PS_I_M: if (ps_i > 1) ps_i -= 1; return true;
	case A_PS_I_P: if (ps_i < 50) ps_i += 1; return true;
	case A_PS_OUT: ps_out = !ps_out; return true;
	case A_RF_SCAN: rf_scan = !rf_scan; return true;
	case A_WF_PREV: if (wf_page > 0) wf_page--; return true;
	case A_WF_NEXT: if (wf_page < 1) wf_page++; return true;
	case A_WF_SCAN: wf_scan_until = SDL_GetTicks() + 1200; return true;
	case A_ST_WIFI: wf_on = !wf_on; return true;
	case A_ST_SOUND: st_sound = !st_sound; return true;
	case A_ST_BR_M: if (st_br > 20) st_br -= 5; return true;
	case A_ST_BR_P: if (st_br < 100) st_br += 5; return true;
	case A_ST_SLEEP: st_sleep = (st_sleep + 1) % 4; return true;
	default: break;
	}
	if (act >= A_RF_MODE && act < A_RF_MODE + N_RF_MODES) { fl_mode = act - A_RF_MODE; return true; }
	if (act >= A_RF_FREQ && act < A_RF_FREQ + N_RF_FREQS) { rf_freq = act - A_RF_FREQ; return true; }
	if (act >= A_WF_NET && act < A_WF_NET + N_WF_NETS) { wf_conn = act - A_WF_NET; return true; }
	return false;
}

static uint32_t press_until;

static bool handle_event(const SDL_Event *ev, int *screen, bool *run) {
	switch (ev->type) {
	case SDL_QUIT: *run = false; return false;
	case SDL_WINDOWEVENT:
		return ev->window.event == SDL_WINDOWEVENT_EXPOSED ||
		       ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
		       ev->window.event == SDL_WINDOWEVENT_RESTORED;
	case SDL_KEYDOWN:
		switch (ev->key.keysym.sym) {
		case SDLK_q: *run = false; return false;
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
			*screen = ev->key.keysym.sym - SDLK_1 + 1;
			return true;
		default: break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (ev->button.button == SDL_BUTTON_LEFT) {
			int act = hit_test(ev->button.x / SCALE, ev->button.y / SCALE);
			if (act == A_NONE) return false;
			press_act = act;
			press_live = true;
			press_until = SDL_GetTicks() + 110;
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
	if (SDL_UpdateTexture(tex, NULL, fb, SCR_W * sizeof(uint16_t)) < 0 ||
	    SDL_RenderClear(ren) < 0 || SDL_RenderCopy(ren, tex, NULL, NULL) < 0) {
		fprintf(stderr, "SDL2 render: %s\n", SDL_GetError());
		return -1;
	}
	SDL_RenderPresent(ren);
	return 0;
}

static int smoke_test(void) {
	SDL_Window *win = SDL_CreateWindow("Gigaspark smoke", 0, 0, WIN_W, WIN_H, SDL_WINDOW_HIDDEN);
	if (!win) return 1;
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
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
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "SDL2: %s\n", SDL_GetError());
		return 1;
	}
	if (smoke) {
		int result = smoke_test();
		SDL_Quit();
		if (result == 0) puts("Gigaspark: 7 pantallas OK, objetivos tactiles >= 80 px");
		return result;
	}

	SDL_Window *win = SDL_CreateWindow("Gigaspark OS - Simulador tactil 800x480",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H, SDL_WINDOW_SHOWN);
	if (!win) {
		fprintf(stderr, "SDL2 window: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
	if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
		SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H) : NULL;
	if (!tex) {
		fprintf(stderr, "SDL2 renderer/texture: %s\n", SDL_GetError());
		if (ren) SDL_DestroyRenderer(ren);
		SDL_DestroyWindow(win);
		SDL_Quit();
		return 1;
	}

	bool run = true, dirty = true;
	int screen = 0, result = 0;
	uint8_t bat = 100;
	uint32_t last_battery = SDL_GetTicks(), last_minute = UINT32_MAX;
	SDL_Event ev;
	while (run) {
		uint32_t frame_start = SDL_GetTicks();
		if (!screen_is_animated(screen) && !dirty) {
			uint32_t now = SDL_GetTicks();
			uint32_t elapsed = now - last_battery;
			uint32_t wait = elapsed >= 30000 ? 1 : 30000 - elapsed;
			uint32_t to_minute = 60000 - now % 60000;
			if (to_minute < wait) wait = to_minute;
			if (wf_scan_until && wf_scan_until > now && wf_scan_until - now < wait)
				wait = wf_scan_until - now;
			if (SDL_WaitEventTimeout(&ev, (int)wait))
				dirty = handle_event(&ev, &screen, &run) || dirty;
		}
		while (SDL_PollEvent(&ev))
			dirty = handle_event(&ev, &screen, &run) || dirty;
		if (!run) break;

		uint32_t now = SDL_GetTicks();
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
			uint32_t elapsed = SDL_GetTicks() - frame_start;
			if (elapsed < FRAME) SDL_Delay(FRAME - elapsed);
		}
	}
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return result;
}
