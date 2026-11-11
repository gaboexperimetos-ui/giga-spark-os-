/*
 * Gigaspark OS - nucleo de interfaz PORTABLE (Material Design 3)
 *
 * Este fichero es el sistema operativo dibujable, sin ninguna dependencia de
 * SDL ni de Zephyr. Renderiza 800x480 RGB565, la resolucion del Arduino GIGA
 * Display Shield real, y registra la zona tactil de cada boton en la misma
 * llamada que lo dibuja: lo que se ve es exactamente lo que responde al dedo.
 *
 * Lo incluyen DOS backends, y es la razon de que exista como fichero aparte:
 *   simulator/main.c        -> ventana SDL + raton/teclado
 *   m7/src/display_giga.c   -> panel ST7701 por LTDC + touch GT911, en la placa
 *
 * Antes vivia dentro de simulator/main.c, asi que el firmware del M7 no podia
 * usarlo: su hilo de UI se limitaba a parpadear un LED. La interfaz solo
 * existia en el simulador. Ahora ambos pintan los mismos pixeles.
 *
 * Se incluye, no se enlaza (igual que ui_material.c): los tests y el bridge
 * acceden a render_screen/hit_test/dispatch/fb directamente.
 *
 * El backend debe definir antes de incluir:
 *   UI_TICKS()          reloj monotono en ms
 *   UI_OS_FB_SECTION    (opcional) seccion del linker para el framebuffer
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UI_TICKS
#error "define UI_TICKS() antes de incluir ui_os.c (reloj monotono en ms)"
#endif

#define SCR_W  800
#define SCR_H  480

/* Cromo Android: estado arriba, app bar, contenido y navegacion abajo. */
#define STATUS_H 36
#define APPBAR_H 80
#define NAV_H    80
#define CX       0
#define CY       (STATUS_H + APPBAR_H)
#define CW       SCR_W
#define CH       (SCR_H - CY - NAV_H)
#define NAV_Y    (SCR_H - NAV_H)
#define PAD      16
#define GAP      12
#define TOUCH_MIN 80

_Static_assert(CH >= 280, "el contenido no puede alojar 3 filas tactiles");
_Static_assert(NAV_H >= TOUCH_MIN, "la barra de navegacion debe ser alcanzable");

/* Framebuffer RGB565 completo: 800*480*2 = 768 KB.
 * En el simulador es BSS normal. En el M7 no cabe en la SRAM interna, asi que
 * el backend define UI_OS_FB_SECTION y lo coloca en la SDRAM de 8 MB de la
 * placa (nodo sdram1). Sigue siendo un array, no un punter: bridge.c y
 * sim_bench.c hacen sizeof(fb). */
#ifdef UI_OS_FB_SECTION
static uint16_t fb[SCR_W * SCR_H] __attribute__((section(UI_OS_FB_SECTION), aligned(32)));
#else
static uint16_t fb[SCR_W * SCR_H];
#endif

static void frect(int x, int y, int w, int h, uint16_t c) {
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	int x1 = x + w, y1 = y + h;
	if (x1 > SCR_W) x1 = SCR_W;
	if (y1 > SCR_H) y1 = SCR_H;
	for (int j = y0; j < y1; j++) {
		uint16_t *row = fb + j * SCR_W;
		for (int i = x0; i < x1; i++) row[i] = c;
	}
}

/* ============================================================
 * ACCIONES
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

/* ============================================================
 * REGISTRO TACTIL
 * ============================================================ */

typedef struct { int16_t x, y, w, h; int act; } Hit;
#define MAX_HITS 80
static Hit hits[MAX_HITS];
static int nhits;
static int press_act = -1;
static bool press_live;
static uint32_t press_until;
static uint32_t wf_scan_until;

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

/* Capa Material: color con alfa, Roboto, widgets. Necesita lo de arriba. */
#include "ui_material.c"

static void utoa(uint32_t v, char *b) {
	char t[16]; int i = 0;
	if (!v) { b[0] = '0'; b[1] = 0; return; }
	while (v) { t[i++] = '0' + v % 10; v /= 10; }
	for (int j = 0; j < i; j++) b[j] = t[i - 1 - j];
	b[i] = 0;
}

static void fmt_tenth(int v, char *b) {
	char n[16]; utoa((uint32_t)(v / 10), n);
	strcpy(b, n);
	size_t l = strlen(b);
	b[l] = '.'; b[l + 1] = '0' + v % 10; b[l + 2] = 0;
}

/* ============================================================
 * ICONOS
 * Trazos gruesos con extremo redondo, como los iconos Material Symbols.
 * ============================================================ */

enum { IC_SCOPE, IC_BOLT, IC_RF, IC_WIFI, IC_TUNE, IC_INFO,
       IC_BACK, IC_HOME, IC_RECENTS, IC_CHECK, IC_LOCK, IC_REFRESH,
       IC_CHEV_L, IC_CHEV_R, IC_SUN, IC_VOLUME, IC_MOON, IC_CLOSE };

static void icon(int cx, int cy, int kind, int s, uint16_t col) {
	int t = s / 8 + 1;                      /* grosor proporcional */
	if (kind == IC_SCOPE) {
		int prevx = cx - s, prevy = cy;
		for (int i = -s; i <= s; i++) {
			int y = cy - (int)(sinf((float)i / (float)s * 3.14159f) * (s * 0.62f));
			if (i > -s) m_line(prevx, prevy, cx + i, y, t, col);
			prevx = cx + i; prevy = y;
		}
	} else if (kind == IC_BOLT) {
		m_line(cx + s / 3, cy - s, cx - s / 3, cy + s / 8, t * 2, col);
		m_line(cx - s / 3, cy + s / 8, cx + s / 4, cy + s / 8, t * 2, col);
		m_line(cx + s / 4, cy + s / 8, cx - s / 4, cy + s, t * 2, col);
	} else if (kind == IC_WIFI) {
		/* Abanico de arcos sobre un punto: el icono clasico de WiFi. */
		int base = cy + s * 2 / 3;
		for (int k = 1; k <= 3; k++) {
			int r = s * k / 3;
			for (int a = -55; a <= 55; a += 2) {
				float rad = (float)a * 0.0174533f;
				m_circle(cx + (int)(sinf(rad) * r), base - (int)(cosf(rad) * r),
				         t / 2, col);
			}
		}
		m_circle(cx, base, t, col);
	} else if (kind == IC_RF) {
		/* Antena emitiendo: mastil vertical y ondas a los lados.
		 * Antes eran arcos como los de WiFi y las dos apps se confundian
		 * en el launcher. */
		m_line(cx, cy - s, cx, cy + s, t, col);
		m_circle(cx, cy - s, t + 1, col);
		m_line(cx - s / 2, cy + s, cx + s / 2, cy + s, t, col);
		for (int k = 1; k <= 2; k++) {
			int r = s * k / 2;
			for (int a = -40; a <= 40; a += 3) {
				float rad = (float)a * 0.0174533f;
				int yy = cy - s + (int)((1.0f - cosf(rad)) * r);
				int xx = (int)(sinf(rad) * r);
				m_circle(cx + s / 3 + xx, yy, t / 2, col);
				m_circle(cx - s / 3 - xx, yy, t / 2, col);
			}
		}
	} else if (kind == IC_TUNE) {
		for (int k = 0; k < 3; k++) {
			int y = cy - s * 2 / 3 + k * (s * 2 / 3);
			m_line(cx - s, y, cx + s, y, t, col);
			int kx = cx + (k == 0 ? -s / 3 : k == 1 ? s / 3 : -s / 6);
			m_circle(kx, y, s / 4, col);
		}
	} else if (kind == IC_INFO) {
		m_ring(cx, cy, s, t, col);
		m_circle(cx, cy - s * 4 / 9, t, col);
		m_line(cx, cy - s / 5, cx, cy + s * 4 / 9, t * 2, col);
	} else if (kind == IC_BACK) {
		/* Triangulo isosceles apuntando a la izquierda, como el back de
		 * Android. La punta va a la izquierda y la base vertical a la
		 * derecha; antes salia un rombo porque el ancho era simetrico. */
		for (int j = -s; j <= s; j++) {
			int w = (s - abs(j)) * 2;
			if (w < 1) w = 1;
			frect(cx + s - w, cy + j, w, 1, col);
		}
	} else if (kind == IC_HOME) {
		m_ring(cx, cy, s, t + 1, col);
	} else if (kind == IC_RECENTS) {
		m_ring_rect(cx - s, cy - s, s * 2, s * 2, s / 3, col);
	} else if (kind == IC_CHECK) {
		m_line(cx - s, cy, cx - s / 4, cy + s * 3 / 5, t * 2, col);
		m_line(cx - s / 4, cy + s * 3 / 5, cx + s, cy - s * 3 / 5, t * 2, col);
	} else if (kind == IC_LOCK) {
		m_rrect(cx - s * 3 / 4, cy - s / 8, s * 3 / 2, s, s / 4, col);
		m_ring(cx, cy - s / 4, s * 3 / 5, t, col);
		frect(cx - s * 3 / 4, cy - s / 8, s * 3 / 2, s / 3, M_CARD);
	} else if (kind == IC_REFRESH) {
		m_ring(cx, cy, s, t + 1, col);
		frect(cx, cy - s - t, s + t, t * 3, M_CARD);
		m_line(cx + s / 2, cy - s, cx + s + t, cy - s / 2, t * 2, col);
	} else if (kind == IC_CHEV_L || kind == IC_CHEV_R) {
		int d = kind == IC_CHEV_L ? 1 : -1;
		m_line(cx + d * s / 2, cy - s, cx - d * s / 2, cy, t * 2, col);
		m_line(cx - d * s / 2, cy, cx + d * s / 2, cy + s, t * 2, col);
	} else if (kind == IC_SUN) {
		m_circle(cx, cy, s * 2 / 5, col);
		for (int a = 0; a < 360; a += 45) {
			float r = (float)a * 0.0174533f;
			m_line(cx + (int)(cosf(r) * s * 3 / 5), cy + (int)(sinf(r) * s * 3 / 5),
			       cx + (int)(cosf(r) * s), cy + (int)(sinf(r) * s), t, col);
		}
	} else if (kind == IC_VOLUME) {
		for (int j = -s / 2; j <= s / 2; j++)
			frect(cx - s, cy + j, s / 2, 1, col);
		m_line(cx - s / 2, cy - s / 2, cx, cy - s, t * 2, col);
		m_line(cx, cy - s, cx, cy + s, t * 2, col);
		m_line(cx, cy + s, cx - s / 2, cy + s / 2, t * 2, col);
	} else if (kind == IC_CLOSE) {
		m_line(cx - s, cy - s, cx + s, cy + s, t * 2, col);
		m_line(cx + s, cy - s, cx - s, cy + s, t * 2, col);
	} else if (kind == IC_MOON) {
		m_circle(cx, cy, s, col);
		m_circle(cx + s / 2, cy - s / 3, s * 3 / 4, M_CARD);
	}
}

/* Boton de icono cuadrado, objetivo tactil completo. */
static void m_ibtn(int x, int y, int w, int h, int kind, bool sel, bool enabled, int act) {
	bool down = press_live && enabled && act != A_NONE && act == press_act;
	uint16_t fill = sel ? M_PRIC : (down ? M_CARD2 : M_CARD);
	uint16_t fg = !enabled ? M_OUT : (sel ? M_PRI : M_TXT);
	if (!enabled) fill = M_SURF;
	m_rrect(x, y, w, h, R_SMALL, fill);
	icon(x + w / 2, y + h / 2, kind, h / 4, fg);
	if (enabled) hit_add(x, y, w, h, act);
}

/* ============================================================
 * CROMO ANDROID
 * ============================================================ */

static const char *app_titles[] = {
	"Gigaspark", "Osciloscopio", "Fuente", "Flipper RF", "WiFi", "Ajustes", "Acerca de"
};
static const uint16_t app_col[] = { M_PRI, M_CYN, M_YEL, M_PRP, M_PRI, M_PRI, M_PRI };

static bool wf_on = true;
static int st_br = 85;

/* Barra de estado: hora a la izquierda, conectividad y bateria a la derecha. */
static void status_bar(uint32_t t, uint8_t bat) {
	frect(0, 0, SCR_W, STATUS_H, M_BG);
	uint32_t mins = t / 60000u;
	char hm[16];
	snprintf(hm, sizeof(hm), "%u:%02u", (unsigned)(9 + mins / 60) % 24, (unsigned)(mins % 60));
	m_text(PAD + 4, (STATUS_H - rf_fonts[F_SMALL].cap) / 2, hm, M_TXT, F_SMALL);

	int rx = SCR_W - PAD;
	/* Bateria: pastilla horizontal con nivel, como Android.
	 * Orden de derecha a izquierda: pastilla, porcentaje, WiFi. Cada uno
	 * con su hueco; antes el icono se montaba sobre el "100%". */
	char pc[16]; utoa(bat, pc); strcat(pc, "%");
	int bx = rx - 31, by = STATUS_H / 2 - 7;
	m_text_r(bx - 10, (STATUS_H - rf_fonts[F_SMALL].cap) / 2, pc, M_TXT, F_SMALL);
	m_rrect(bx, by, 28, 14, 4, M_OUT);
	m_rrect(bx + 2, by + 2, 24 * bat / 100, 10, 3, bat > 20 ? M_GRN : M_RED);
	frect(bx + 28, by + 4, 3, 6, M_OUT);
	if (wf_on) {
		int wx = bx - 10 - m_text_w(pc, F_SMALL) - 20;
		icon(wx, STATUS_H / 2 + 3, IC_WIFI, 8, M_TXT);
	}
}

/* App bar: titulo grande a la izquierda, acciones a la derecha. */
static void app_bar(int screen, int act1, int ic1, int act2, int ic2, int act3, int ic3) {
	frect(0, STATUS_H, SCR_W, APPBAR_H, M_BG);
	m_text(PAD + 4, STATUS_H + (APPBAR_H - rf_fonts[F_TITLE].cap) / 2,
	       app_titles[screen], app_col[screen], F_TITLE);
	int x = SCR_W - PAD;
	int acts[3] = { act3, act2, act1 }, ics[3] = { ic3, ic2, ic1 };
	for (int i = 0; i < 3; i++) {
		if (ics[i] < 0) continue;
		x -= APPBAR_H;
		m_ibtn(x, STATUS_H, APPBAR_H, APPBAR_H, ics[i], false, acts[i] != A_NONE, acts[i]);
		x -= 8;
	}
}

/* Barra de navegacion Android de 3 botones: atras, inicio, recientes. */
static void nav_bar(int screen) {
	frect(0, NAV_Y, SCR_W, NAV_H, M_SURF);
	frect(0, NAV_Y, SCR_W, 1, M_OUT);
	int bw = 120, gap = 40;
	int total = bw * 3 + gap * 2;
	int x = (SCR_W - total) / 2;
	int kinds[3] = { IC_BACK, IC_HOME, IC_RECENTS };
	int acts[3] = { A_BACK, A_HOME, A_MENU };
	for (int i = 0; i < 3; i++) {
		/* En el launcher no hay nada detras: atras se dibuja apagado. */
		bool en = !(i == 0 && screen == 0);
		bool down = press_live && en && press_act == acts[i];
		if (down) m_rrect(x + 16, NAV_Y + 10, bw - 32, NAV_H - 20, R_SMALL, M_CARD2);
		icon(x + bw / 2, NAV_Y + NAV_H / 2, kinds[i], 13, en ? M_TXT : M_OUT);
		if (en) hit_add(x, NAV_Y, bw, NAV_H, acts[i]);
		x += bw + gap;
	}
}

static void chrome(uint32_t t, uint8_t bat, int screen) {
	frect(0, 0, SCR_W, SCR_H, M_BG);
	status_bar(t, bat);
	nav_bar(screen);
}

/* ============================================================
 * LAUNCHER
 * Rejilla de iconos circulares con etiqueta, como el launcher de Android.
 * ============================================================ */

#define LA_N N_APPS
static int la_sel;
static const char *la_nm[] = {
	"Osciloscopio", "Fuente", "Flipper RF", "WiFi", "Ajustes", "Acerca de"
};
static const int la_ic[] = { IC_SCOPE, IC_BOLT, IC_RF, IC_WIFI, IC_TUNE, IC_INFO };
static const uint16_t la_col[] = { M_CYN, M_YEL, M_PRP, M_PRI, M_GRN, M_MUT };

static void app_launch(uint32_t t, uint8_t bat) {
	chrome(t, bat, 0);
	app_bar(0, A_NONE, -1, A_NONE, -1, A_NONE, -1);
	int cols = 3, rows = 2;
	int tw = (CW - 2 * PAD - (cols - 1) * GAP) / cols;
	int th = (CH - (rows - 1) * GAP) / rows;
	for (int i = 0; i < LA_N; i++) {
		int c = i % cols, r = i / cols;
		int x = CX + PAD + c * (tw + GAP), y = CY + r * (th + GAP);
		bool down = press_live && press_act == A_APP + i;
		bool sel = i == la_sel;
		m_rrect(x, y, tw, th, R_CARD, down ? M_CARD2 : (sel ? M_CARD : M_SURF));
		int icx = x + tw / 2, icy = y + th / 2 - 16;
		/* Icono sobre circulo tonal, sello del launcher Android. */
		m_circle(icx, icy, 34, M_CARD2);
		icon(icx, icy, la_ic[i], 19, la_col[i]);
		m_text_c(x + tw / 2, y + th - 34, la_nm[i], M_TXT, F_BODY);
		hit_add(x, y, tw, th, A_APP + i);
	}
}

/* ============================================================
 * OSCILOSCOPIO
 * ============================================================ */

static bool sc_run = true, sc_ch1 = true, sc_ch2 = true;
static int sc_tb = 3;
static float sc_phase;
static const char *tb_lbl[] = {
	"100 µs", "200 µs", "500 µs", "1 ms", "2 ms", "5 ms", "10 ms"
};

static void app_scope(uint32_t t, uint8_t bat) {
	chrome(t, bat, 1);
	app_bar(1, A_SC_AUTO, IC_REFRESH, A_NONE, -1, A_NONE, -1);
	int gh = CH - 88 - GAP;
	int gx = CX + PAD, gy = CY, gw = CW - 2 * PAD;
	m_card(gx, gy, gw, gh);

	int px0 = gx + 16, py0 = gy + 40, pw = gw - 32, ph = gh - 56;
	/* Retícula tenue */
	for (int i = 1; i < 10; i++)
		frect(px0 + pw * i / 10, py0, 1, ph, M_CARD2);
	for (int i = 1; i < 4; i++)
		frect(px0, py0 + ph * i / 4, pw, 1, M_CARD2);
	frect(px0, py0 + ph / 2, pw, 1, M_OUT);

	char lbl[48];
	snprintf(lbl, sizeof(lbl), "Base de tiempo  %s", tb_lbl[sc_tb]);
	m_text(gx + 16, gy + 12, lbl, M_MUT, F_SMALL);
	m_text_r(gx + gw - 16, gy + 12, sc_run ? "En marcha" : "Pausado",
	         sc_run ? M_GRN : M_YEL, F_SMALL);

	if (sc_run) sc_phase += 0.09f;
	float cycles = 1.0f + (float)sc_tb * 0.7f;
	if (sc_ch1) {
		int prevy = py0 + ph / 2;
		for (int i = 0; i <= pw; i++) {
			float a = (float)i / (float)pw * cycles * 6.28318f + sc_phase;
			int y = py0 + ph / 2 - (int)(sinf(a) * (ph * 0.36f));
			if (i) m_trace(px0 + i, prevy, y, 3, M_CYN);
			prevy = y;
		}
	}
	if (sc_ch2) {
		int prevy = py0 + ph / 2;
		for (int i = 0; i <= pw; i++) {
			float a = (float)i / (float)pw * cycles * 6.28318f + sc_phase * 0.7f;
			/* Onda cuadrada: el trazo vertical une los dos niveles. */
			int y = py0 + ph / 2 + (sinf(a) >= 0 ? -(ph * 3 / 10) : (ph * 3 / 10));
			if (i) m_trace(px0 + i, prevy, y, 3, M_YEL);
			prevy = y;
		}
	}

	int y = gy + gh + GAP, h = 88;
	int bw = (gw - 3 * 8) / 4;
	m_btn(gx, y, bw, h, sc_run ? "Pausar" : "Reanudar", MB_FILL, A_SC_RUN);
	m_chip(gx + (bw + 8), y, bw, h, "CH 1", sc_ch1, A_SC_CH1);
	m_chip(gx + 2 * (bw + 8), y, bw, h, "CH 2", sc_ch2, A_SC_CH2);
	int hx = gx + 3 * (bw + 8);
	m_ibtn(hx, y, bw / 2 - 4, h, IC_CHEV_L, false, sc_tb > 0, A_SC_TB_M);
	m_ibtn(hx + bw / 2 + 4, y, bw / 2 - 4, h, IC_CHEV_R, false, sc_tb < 6, A_SC_TB_P);
}

/* ============================================================
 * FUENTE DE ALIMENTACION
 * ============================================================ */

static int ps_v = 50, ps_i = 10;   /* decimas: 5.0 V, 1.0 A */
static bool ps_out;

static void psu_card(int x, int y, int w, int h, const char *title,
                     const char *val, const char *unit, const char *range,
                     int pct, uint16_t col, int am, int ap) {
	m_card(x, y, w, h);
	/* Cursor vertical: cada bloque avanza lo que mide. Asi el rango no
	 * puede acabar debajo de los botones, que era el bug anterior. */
	int cy = y + 8;
	m_text_c(x + w / 2, cy, title, M_MUT, F_SMALL);
	cy += rf_fonts[F_SMALL].cap + 4;

	int vw = m_text_w(val, F_BIG), uw = m_text_w(unit, F_BODY);
	int sx = x + (w - vw - uw - 8) / 2;
	m_text(sx, cy, val, col, F_BIG);
	m_text(sx + vw + 8, cy + rf_fonts[F_BIG].cap - rf_fonts[F_BODY].cap,
	       unit, M_MUT, F_BODY);
	cy += rf_fonts[F_BIG].cap + 6;

	m_bar(x + 24, cy, w - 48, 8, pct, col);
	cy += 8 + 4;
	m_text_c(x + w / 2, cy, range, M_MUT, F_SMALL);
	cy += rf_fonts[F_SMALL].cap + 6;

	/* Lo que queda es para los botones, con 8 px de aire abajo. */
	int bh = y + h - 8 - cy;
	if (bh < TOUCH_MIN) bh = TOUCH_MIN;
	int bw = (w - 48 - 12) / 2;
	m_ibtn(x + 24, cy, bw, bh, IC_CHEV_L, false, true, am);
	m_ibtn(x + 24 + bw + 12, cy, bw, bh, IC_CHEV_R, false, true, ap);
}

static void app_psu(uint32_t t, uint8_t bat) {
	chrome(t, bat, 2);
	app_bar(2, A_NONE, -1, A_NONE, -1, A_NONE, -1);
	int ch = CH - 88 - GAP;
	int cw = (CW - 2 * PAD - GAP) / 2;
	char v[24], a[24], r[32];
	fmt_tenth(ps_v, v); fmt_tenth(ps_i, a);
	snprintf(r, sizeof(r), "0 – 24.0 V");
	psu_card(CX + PAD, CY, cw, ch, "Voltaje", v, "V", r, ps_v * 100 / 240, M_GRN,
	         A_PS_V_M, A_PS_V_P);
	snprintf(r, sizeof(r), "0 – 5.0 A");
	psu_card(CX + PAD + cw + GAP, CY, cw, ch, "Corriente máx.", a, "A", r, ps_i * 100 / 50, M_YEL,
	         A_PS_I_M, A_PS_I_P);
	m_btn(CX + PAD, CY + ch + GAP, CW - 2 * PAD, 88,
	      ps_out ? "Apagar salida" : "Encender salida",
	      ps_out ? MB_DANGER : MB_FILL, A_PS_OUT);
}

/* ============================================================
 * FLIPPER RF
 * ============================================================ */

static int fl_mode;
static const char *fl_modes[] = { "Sub-GHz", "NFC", "IR", "GPIO" };
static int rf_freq = 1;
static const char *rf_lbl[] = { "315", "433.92", "868.35", "915" };
static bool rf_scan;

static void app_flip(uint32_t t, uint8_t bat) {
	chrome(t, bat, 3);
	app_bar(3, A_RF_SCAN, IC_REFRESH, A_NONE, -1, A_NONE, -1);
	int y = CY;
	int w = CW - 2 * PAD;
	int fh = 92;
	m_card(CX + PAD, y, w, fh);
	m_text(CX + PAD + 20, y + 12, "Frecuencia", M_MUT, F_SMALL);
	char f[32];
	snprintf(f, sizeof(f), "%s MHz", rf_lbl[rf_freq]);
	m_text(CX + PAD + 20, y + 36, f, M_PRP, F_NUM);
	m_text_r(CX + PAD + w - 20, y + 40, rf_scan ? "Escaneando…" : fl_modes[fl_mode],
	         rf_scan ? M_GRN : M_MUT, F_BODY);
	if (rf_scan) {
		int bx = CX + PAD + 20;
		m_bar(bx, y + fh - 14, w - 40, 6, (int)(t / 12 % 100), M_GRN);
	}
	y += fh + 8;
	int cw = (w - 3 * 8) / 4;
	for (int i = 0; i < N_RF_MODES; i++)
		m_chip(CX + PAD + i * (cw + 8), y, cw, 88, fl_modes[i], i == fl_mode, A_RF_MODE + i);
	y += 88 + 8;
	for (int i = 0; i < N_RF_FREQS; i++)
		m_chip(CX + PAD + i * (cw + 8), y, cw, 88, rf_lbl[i], i == rf_freq, A_RF_FREQ + i);
}

/* ============================================================
 * WIFI
 * ============================================================ */

static const char *wf_nm[] = {
	"Gigaspark_AP", "MiFibra-5G", "MOVISTAR_7F2E", "TP-LINK_4A20", "Invitados"
};
static const int wf_sig[] = { 95, 85, 72, 55, 40 };
static int wf_conn;
static int wf_page;
#define WF_PER 3
#define WF_PAGES ((N_WF_NETS + WF_PER - 1) / WF_PER)

static void app_wifi(uint32_t t, uint8_t bat) {
	chrome(t, bat, 4);
	bool scanning = wf_scan_until != 0;
	/* app_bar coloca del final hacia la izquierda: el primer par acaba
	 * mas a la izquierda. Orden visual correcto: < > refrescar. */
	app_bar(4, wf_page > 0 ? A_WF_PREV : A_NONE, IC_CHEV_L,
	        wf_page < WF_PAGES - 1 ? A_WF_NEXT : A_NONE, IC_CHEV_R,
	        A_WF_SCAN, IC_REFRESH);
	if (!wf_on) {
		m_card(CX + PAD, CY, CW - 2 * PAD, 140);
		m_text_c(CW / 2, CY + 34, "WiFi apagado", M_TXT, F_TITLE);
		m_text_c(CW / 2, CY + 84, "Actívalo en Ajustes", M_MUT, F_BODY);
		return;
	}
	int rowh = (CH - 2 * 10) / WF_PER;
	int y = CY;
	for (int k = 0; k < WF_PER; k++) {
		int i = wf_page * WF_PER + k;
		if (i >= N_WF_NETS) break;
		bool conn = i == wf_conn;
		bool down = press_live && press_act == A_WF_NET + i;
		m_rrect(CX + PAD, y, CW - 2 * PAD, rowh, R_CARD,
		        conn ? M_PRIC : (down ? M_CARD2 : M_CARD));
		int icx = CX + PAD + 44;
		icon(icx, y + rowh / 2 + 4, IC_WIFI, 17, conn ? M_PRI : M_MUT);
		int tx = CX + PAD + 84;
		m_text_ell(tx, y + rowh / 2 - 26, 300, wf_nm[i], conn ? M_PRI : M_TXT, F_BODY);
		m_text(tx, y + rowh / 2 + 6, conn ? "Conectado" : "Segura · WPA2",
		       M_MUT, F_SMALL);
		int rx = CX + CW - PAD - 24;
		if (conn) { icon(rx - 14, y + rowh / 2, IC_CHECK, 12, M_PRI); rx -= 48; }
		else if (i != 0) icon(rx - 14, y + rowh / 2, IC_LOCK, 11, M_MUT);
		char pc[16]; utoa((uint32_t)wf_sig[i], pc); strcat(pc, "%");
		m_text_r(rx - 40, y + rowh / 2 - 22, pc, M_MUT, F_SMALL);
		m_bar(rx - 130, y + rowh / 2 + 6, 90, 8, wf_sig[i], conn ? M_PRI : M_GRN);
		hit_add(CX + PAD, y, CW - 2 * PAD, rowh, A_WF_NET + i);
		y += rowh + 10;
	}
	if (scanning)
		m_text_c(CW / 2, NAV_Y - 22, "Buscando redes…", M_GRN, F_SMALL);
}

/* ============================================================
 * AJUSTES
 * Rejilla 2x2 de tarjetas con switch, slider y selector.
 * ============================================================ */

static bool st_sound = true;
static int st_sleep = 1;
static const int st_sleeps[] = { 15, 30, 60, 0 };

static void app_set(uint32_t t, uint8_t bat) {
	chrome(t, bat, 5);
	app_bar(5, A_NONE, -1, A_NONE, -1, A_NONE, -1);
	int cols = 2, rows = 2;
	int tw = (CW - 2 * PAD - GAP) / cols;
	/* 8 px de aire antes de la barra de navegacion. */
	int th = (CH - GAP - 8) / rows;
	for (int i = 0; i < 4; i++) {
		int c = i % cols, r = i / cols;
		int x = CX + PAD + c * (tw + GAP), y = CY + r * (th + GAP);
		bool down = press_live && press_act == A_ST_SLEEP && i == 3;
		m_rrect(x, y, tw, th, R_CARD, down ? M_CARD2 : M_CARD);

		/* Cabecera comun: icono, titulo y estado. Nunca se pisa con el
		 * control porque el control va a la derecha o debajo del texto. */
		int kind = i == 0 ? IC_WIFI : i == 1 ? IC_SUN : i == 2 ? IC_VOLUME : IC_MOON;
		uint16_t ic = i == 0 ? (wf_on ? M_PRI : M_MUT) : i == 1 ? M_YEL
		            : i == 2 ? (st_sound ? M_PRI : M_MUT) : M_PRP;
		icon(x + 40, y + 42, kind, 16, ic);
		static const char *names[] = { "WiFi", "Brillo", "Sonido táctil", "Suspensión" };
		m_text(x + 76, y + 24, names[i], M_TXT, F_BODY);

		char sub[32];
		if (i == 0) snprintf(sub, sizeof(sub), "%s", wf_on ? "Activado" : "Desactivado");
		else if (i == 1) { utoa((uint32_t)st_br, sub); strcat(sub, "%"); }
		else if (i == 2) snprintf(sub, sizeof(sub), "%s", st_sound ? "Activado" : "Silencio");
		else if (st_sleeps[st_sleep] == 0) snprintf(sub, sizeof(sub), "Nunca");
		else snprintf(sub, sizeof(sub), "%d segundos", st_sleeps[st_sleep]);
		m_text(x + 76, y + 52, sub, M_MUT, F_SMALL);

		if (i == 0 || i == 2) {
			/* Switch centrado en vertical a la derecha, como en Ajustes
			 * de Android. Antes iba abajo y descompensaba la tarjeta. */
			m_switch(x + tw - 128, y + (th - 60) / 2, i == 0 ? wf_on : st_sound,
			         i == 0 ? A_ST_WIFI : A_ST_SOUND);
		} else if (i == 1) {
			/* Slider debajo del texto; los botones a la derecha, centrados.
			 * Antes el 85% caia encima del track. */
			m_slider(x + 24, y + th - 40, tw - 48 - 184, st_br, M_YEL);
			m_ibtn(x + tw - 184, y + (th - 80) / 2, 80, 80, IC_CHEV_L,
			       false, st_br > 20, A_ST_BR_M);
			m_ibtn(x + tw - 96, y + (th - 80) / 2, 80, 80, IC_CHEV_R,
			       false, st_br < 100, A_ST_BR_P);
		} else {
			/* Preferencia de lista: toda la tarjeta cicla el valor y un
			 * chevron lo indica. Un boton "Cambiar" dentro no cabia sin
			 * tapar el titulo ni el estado. */
			icon(x + tw - 40, y + th / 2, IC_CHEV_R, 12, M_MUT);
			hit_add(x, y, tw, th, A_ST_SLEEP);
		}
	}
}

/* ============================================================
 * ACERCA DE
 * ============================================================ */

static void app_about(uint32_t t, uint8_t bat) {
	chrome(t, bat, 6);
	app_bar(6, A_NONE, -1, A_NONE, -1, A_NONE, -1);
	static const char *k[] = { "Equipo", "MCU", "Panel", "Táctil", "Sistema", "Memoria" };
	static const char *v[] = {
		"Arduino GIGA R1 + Display Shield",
		"STM32H747XI · M7 480 + M4 240 MHz",
		"480×800 nativo · ST7701 MIPI DSI",
		"Goodix GT911 · I²C · 5 puntos",
		"Gigaspark OS · Zephyr RTOS",
		"Buddy + zRAM"
	};
	int n = 6;
	int rowh = (CH - (n - 1) * 6) / n;
	for (int i = 0; i < n; i++) {
		int y = CY + i * (rowh + 6);
		m_rrect(CX + PAD, y, CW - 2 * PAD, rowh, R_SMALL, M_CARD);
		m_text(CX + PAD + 20, y + (rowh - rf_fonts[F_SMALL].cap) / 2, k[i], M_MUT, F_SMALL);
		m_text_ell(CX + PAD + 160, y + (rowh - rf_fonts[F_BODY].cap) / 2,
		           CW - 2 * PAD - 180, v[i], M_TXT, F_BODY);
	}
}

/* ============================================================
 * AJUSTES RAPIDOS (panel de recientes)
 * ============================================================ */

static bool qs_open;

static void draw_qs(uint32_t t, uint8_t bat) {
	(void)t; (void)bat;
	/* Scrim semitransparente sobre el contenido, como Android. */
	for (int j = 0; j < SCR_H; j++)
		for (int i = 0; i < SCR_W; i++)
			fb[j * SCR_W + i] = mix565(fb[j * SCR_W + i], 0x0000, 150);
	nhits = 0;
	int w = 620, h = 300, x = (SCR_W - w) / 2, y = (SCR_H - h) / 2;
	hit_add(0, 0, SCR_W, SCR_H, A_QS_CLOSE);
	m_rrect(x, y, w, h, R_CARD, M_CARD);
	m_text(x + 24, y + 20, "Ajustes rápidos", M_TXT, F_TITLE);
	m_ibtn(x + w - 88, y + 12, 80, 80, IC_CLOSE, false, true, A_QS_CLOSE);

	int ry = y + 96;
	m_rrect(x + 24, ry, w - 48, 84, R_SMALL, M_CARD2);
	icon(x + 60, ry + 42, IC_SUN, 15, M_YEL);
	m_slider(x + 96, ry + 34, w - 48 - 96 - 184, st_br, M_YEL);
	m_ibtn(x + w - 208, ry + 2, 80, 80, IC_CHEV_L, false, st_br > 20, A_ST_BR_M);
	m_ibtn(x + w - 120, ry + 2, 80, 80, IC_CHEV_R, false, st_br < 100, A_ST_BR_P);

	int ty = ry + 96;
	int tw = (w - 48 - 12) / 2;
	m_chip(x + 24, ty, tw, 84, wf_on ? "WiFi ON" : "WiFi OFF", wf_on, A_ST_WIFI);
	m_chip(x + 36 + tw, ty, tw, 84, st_sound ? "Sonido ON" : "Sonido OFF", st_sound, A_ST_SOUND);
}

/* ============================================================
 * RENDER
 * ============================================================ */

/* Factor de retroiluminacion en 0..255.
 *
 * El brillo NO se aplica al framebuffer. En el hardware real es el PWM de la
 * retroiluminacion: escala la luz emitida, no los valores del panel. Aplicarlo
 * dentro del RGB565 obligaba a recuantizar tonos ya oscuros, donde solo hay 32
 * o 64 niveles, y el gris viraba a verde (la tarjeta pasaba de (33,36,41) a
 * (24,32,33), con verde==azul). Aqui se entrega el factor para aplicarlo tras
 * expandir a 8 bits, que es donde hay precision. */
static uint8_t backlight_scale(void) {
	return (uint8_t)(40 + st_br * 215 / 100);
}

static bool screen_is_animated(int screen) {
	return screen == 1 || (screen == 3 && rf_scan) || press_live || wf_scan_until != 0;
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
	case A_WF_SCAN: wf_scan_until = UI_TICKS() + 1200; return true;
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
