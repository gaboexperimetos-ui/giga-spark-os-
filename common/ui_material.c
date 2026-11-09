/* ============================================================
 * CAPA MATERIAL DESIGN 3
 *
 * Por que existe: la interfaz anterior se dibujaba con una fuente bitmap 5x7
 * escalada por enteros y widgets propios, y el resultado parecia una terminal
 * de los 80, no Android. Android se reconoce por tres cosas concretas:
 *   1. Roboto con antialiasing (no bitmap escalado).
 *   2. Superficies tonales elevadas: el color de fondo se aclara por niveles,
 *      no se usan bordes brillantes por todas partes.
 *   3. Componentes reconocibles: switch de pastilla, botones filled/tonal,
 *      barra de estado arriba y barra de navegacion abajo.
 *
 * Este fichero se incluye desde main.c y aporta primitivas con alfa real,
 * tipografia Roboto y los widgets. Todo sigue escribiendo en un framebuffer
 * RGB565, el formato del panel real.
 * ============================================================ */

#include "roboto_font.h"

#include "ui_theme.h"

/* ---------- Superficies M3 (dark, esquema azul) ----------
 * En M3 la elevacion se expresa aclarando la superficie, no con sombras
 * duras. Estos valores son los tonos del esquema, cuantizados a RGB565
 * REDONDEANDO al mas cercano, no truncando.
 *
 * Importa: truncar (r>>3) en colores oscuros desplaza el tono. CARD2 truncado
 * daba (33,48,49), con verde==azul, y las tarjetas se veian verdosas;
 * redondeando da (41,48,57), el gris azulado correcto. */
#define M_BG UI_RGB565(UI_BG)   /* #101418  fondo */
#define M_SURF UI_RGB565(UI_SURFACE)   /* #151A1F  superficie 1 */
#define M_CARD UI_RGB565(UI_CARD)   /* #1D2328  superficie 2 (tarjetas) */
#define M_CARD2 UI_RGB565(UI_CARD_HIGH)   /* #263036  superficie 3 (pulsado / hover) */
#define M_OUT UI_RGB565(UI_OUTLINE)   /* #39434B  outline variant */
#define M_PRI UI_RGB565(UI_PRIMARY)   /* #A8C7FA  primary */
#define M_PRIC UI_RGB565(UI_PRIMARY_CONT)   /* #1B3A5C  primary container */
#define M_ONPRI UI_RGB565(UI_ON_PRIMARY)   /* #0A2A4A  on primary container */
#define M_TXT UI_RGB565(UI_TEXT)   /* #E3E3E8  on surface */
#define M_MUT UI_RGB565(UI_TEXT_MUTED)   /* #A5ADB5  on surface variant */
#define M_GRN UI_RGB565(UI_GREEN)   /* #6DD58C */
#define M_GRNC UI_RGB565(UI_GREEN_CONT)   /* #11331F */
#define M_RED UI_RGB565(UI_RED)   /* #FFB4AB */
#define M_REDC UI_RGB565(UI_RED_CONT)   /* #4A1B18 */
#define M_YEL UI_RGB565(UI_YELLOW)   /* #FFD580 */
#define M_ORG UI_RGB565(UI_ORANGE)   /* #FFB77C */
#define M_PRP UI_RGB565(UI_PURPLE)   /* #D0BCFF */
#define M_CYN UI_RGB565(UI_CYAN)   /* #80DEEA */

/* Radios M3: las tarjetas son notablemente mas redondas que en M2. */
#define R_CARD    24
#define R_BTN     28
#define R_SMALL   16

/* ---------- Alfa sobre RGB565 ---------- */

/* Mezcla con alfa en RGB565, REDONDEANDO el delta.
 *
 * Truncar no sirve: en superficies oscuras los canales son diminutos (la
 * tarjeta es R=3,G=8,B=5) y la division entera pierde proporcionalmente mas
 * en unos canales que en otros, asi que el gris vira de tono. Un scrim negro
 * sobre la tarjeta daba (16,16,24) truncando, azulado, frente a (8,12,16)
 * redondeando, que mantiene la proporcion del gris original. Esto afecta a
 * todo el antialiasing del texto y de los iconos, no solo al scrim. */
static int mix_ch(int d, int s, int a) {
	int num = (s - d) * a;
	int q = (int)((labs(num) + 127) / 255);
	return num >= 0 ? d + q : d - q;
}

static uint16_t mix565(uint16_t dst, uint16_t src, int a) {
	if (a <= 0) return dst;
	if (a >= 255) return src;
	int r = mix_ch((dst >> 11) & 31, (src >> 11) & 31, a);
	int g = mix_ch((dst >> 5) & 63, (src >> 5) & 63, a);
	int b = mix_ch(dst & 31, src & 31, a);
	return (uint16_t)((r << 11) | (g << 5) | b);
}

static void px_a(int x, int y, uint16_t c, int a) {
	if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H && a > 0)
		fb[y * SCR_W + x] = mix565(fb[y * SCR_W + x], c, a);
}

/* Rectangulo redondeado con borde antialiasado.
 * El radio se resuelve por distancia al centro de la esquina y se convierte
 * en cobertura parcial en el pixel del borde: sin esto las esquinas se ven
 * dentadas y el parecido con Android se pierde. */
static void m_rrect(int x, int y, int w, int h, int r, uint16_t c) {
	if (w <= 0 || h <= 0) return;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	for (int j = 0; j < h; j++) {
		int yy = y + j;
		if (yy < 0 || yy >= SCR_H) continue;
		int inset = 0;
		if (j < r || j >= h - r) {
			int cy = j < r ? r : h - 1 - r;
			int dy = j - cy;
			if (dy < 0) dy = -dy;
			float d = sqrtf((float)(r * r - dy * dy));
			inset = r - (int)(d + 0.5f);
		}
		int x0 = x + inset, x1 = x + w - 1 - inset;
		if (x1 < x0) continue;
		frect(x0, yy, x1 - x0 + 1, 1, c);
		/* Suavizado del extremo: medio tono en el pixel siguiente. */
		if (inset > 0) {
			px_a(x0 - 1, yy, c, 110);
			px_a(x1 + 1, yy, c, 110);
		}
	}
}

/* Contorno de rectangulo redondeado (1 px), para botones outline y chips.
 * Se dibuja como diferencia de dos radios, con cobertura parcial en el borde. */
static void m_ring_rect(int x, int y, int w, int h, int r, uint16_t c) {
	if (w <= 0 || h <= 0) return;
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	/* Solo se recorren las 4 esquinas y los 4 lados de 1 px. La version
	 * anterior recorria w*h calculando una raiz por pixel interior que
	 * siempre daba alfa 0: en un chip de 180x88 eran 15840 px para dibujar
	 * ~1400. Mismo resultado, ~11x menos trabajo. */
	for (int cj = 0; cj < 2; cj++) {
		for (int ci = 0; ci < 2; ci++) {
			int i0 = ci ? w - r : 0, j0 = cj ? h - r : 0;
			for (int j = j0; j < j0 + r; j++) {
				for (int i = i0; i < i0 + r; i++) {
					int kx = i < r ? r : w - 1 - r;
					int ky = j < r ? r : h - 1 - r;
					float d = sqrtf((float)((i - kx) * (i - kx) + (j - ky) * (j - ky)));
					float edge = (float)r - d;
					int a = 0;
					if (edge >= 1.0f) a = 0;
					else if (edge > 0) a = (int)((1.0f - edge) * 255);
					px_a(x + i, y + j, c, a);
				}
			}
		}
	}
	/* Lados rectos: fila superior e inferior, columnas izquierda y derecha. */
	if (w - 2 * r > 0) {
		frect(x + r, y, w - 2 * r, 1, c);
		frect(x + r, y + h - 1, w - 2 * r, 1, c);
	}
	if (h - 2 * r > 0) {
		frect(x, y + r, 1, h - 2 * r, c);
		frect(x + w - 1, y + r, 1, h - 2 * r, c);
	}
}

static void m_circle(int cx, int cy, int r, uint16_t c) {
	for (int j = -r; j <= r; j++) {
		float d = sqrtf((float)(r * r - j * j));
		int half = (int)(d + 0.5f);
		frect(cx - half, cy + j, half * 2 + 1, 1, c);
		px_a(cx - half - 1, cy + j, c, 110);
		px_a(cx + half + 1, cy + j, c, 110);
	}
}

/* Anillo de grosor th con borde antialiasado.
 * La cobertura es la distancia al borde exterior e interior del anillo: un
 * pixel esta dentro si r-th <= d <= r. La version anterior combinaba mal los
 * dos limites y exigia d >= r-th/2 y d <= r-th+0.5 a la vez, rango vacio, asi
 * que HOME, info, candado y refrescar no se dibujaban nunca. */
static void m_ring(int cx, int cy, int r, int th, uint16_t c) {
	if (r <= 0) return;
	if (th < 1) th = 1;
	if (th > r) th = r;
	float ro = (float)r + 0.5f;          /* borde exterior */
	float ri = (float)(r - th) - 0.5f;   /* borde interior */
	for (int j = -r - 1; j <= r + 1; j++) {
		for (int i = -r - 1; i <= r + 1; i++) {
			float d = sqrtf((float)(i * i + j * j));
			float cov = ro - d;
			float inner = d - ri;
			if (inner < cov) cov = inner;
			if (cov <= 0) continue;
			int a = cov >= 1.0f ? 255 : (int)(cov * 255.0f);
			px_a(cx + i, cy + j, c, a);
		}
	}
}

/* Traza de osciloscopio: une dos muestras consecutivas con un tramo vertical.
 *
 * m_line() estampaba un disco por paso (~3.9 us llamada) y la onda la llama
 * una vez por columna, dos canales: ~1500 llamadas por frame. Para una curva
 * muestreada por columna basta un span vertical entre la muestra anterior y
 * la actual, que ademas dibuja las transiciones verticales de la onda
 * cuadrada sin huecos. Sin sqrt ni discos. */
static void m_trace(int x, int y0, int y1, int th, uint16_t c) {
	int lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
	frect(x - th / 2, lo - th / 2, th, hi - lo + th, c);
}

/* Linea gruesa con extremos redondos, para iconos. */
static void m_line(int x0, int y0, int x1, int y1, int th, uint16_t c) {
	int dx = x1 - x0, dy = y1 - y0;
	/* Un paso por pixel del eje mayor: con th >= 2 los discos se solapan
	 * y no quedan huecos. Sobremuestrear al doble solo duplicaba coste. */
	int steps = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) + 1;
	for (int i = 0; i <= steps; i++) {
		float t = (float)i / (float)steps;
		int x = x0 + (int)(dx * t), y = y0 + (int)(dy * t);
		m_circle(x, y, th / 2, c);
	}
}

/* ---------- Tipografia Roboto ---------- */

enum { F_BODY = RF_R24, F_SMALL = RF_R18, F_TITLE = RF_M34, F_BTN = RF_M24,
       F_BIG = RF_R48, F_NUM = RF_M34 };

/* Decodifica un codepoint UTF-8 y lo mapea al indice del atlas. */
static int glyph_index(const char **p) {
	const unsigned char *s = (const unsigned char *)*p;
	unsigned cp;
	if (s[0] < 0x80) { cp = s[0]; *p += 1; }
	else if ((s[0] & 0xE0) == 0xC0 && s[1]) {
		cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); *p += 2;
	} else if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2]) {
		cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3;
	} else { cp = s[0]; *p += 1; }

	if (cp >= 32 && cp <= 126) return (int)(cp - 32);
	for (int i = 0; i < (int)(sizeof(rf_extra_cp) / sizeof(rf_extra_cp[0])); i++)
		if (rf_extra_cp[i] == cp) return RF_FIRST_EXTRA + i;
	return '?' - 32;
}

static int m_text_w(const char *s, int face) {
	const RGlyph *g = rf_fonts[face].g;
	int w = 0;
	while (*s) w += g[glyph_index(&s)].adv;
	return w;
}

/* Dibuja texto con el baseline en y+cap, de modo que 'y' es el borde superior
 * visual de las mayusculas. Asi centrar en un boton no depende de las
 * descendentes de letras como 'g' o 'p'. */
static void m_text(int x, int y, const char *s, uint16_t col, int face) {
	const RFont *f = &rf_fonts[face];
	int base = y + f->cap;
	while (*s) {
		const RGlyph *gl = &f->g[glyph_index(&s)];
		if (gl->w && gl->h) {
			int gx = x + gl->dx, gy = base + gl->dy;
			int stride = (gl->w + 1) / 2;
			for (int j = 0; j < gl->h; j++) {
				const uint8_t *row = rf_data + gl->off + j * stride;
				for (int i = 0; i < gl->w; i++) {
					int v = (i & 1) ? (row[i >> 1] & 15) : (row[i >> 1] >> 4);
					if (v) px_a(gx + i, gy + j, col, v * 17);
				}
			}
		}
		x += gl->adv;
	}
}

static void m_text_c(int cx, int y, const char *s, uint16_t col, int face) {
	m_text(cx - m_text_w(s, face) / 2, y, s, col, face);
}

static void m_text_r(int right, int y, const char *s, uint16_t col, int face) {
	m_text(right - m_text_w(s, face), y, s, col, face);
}

/* Centrado optico en una caja usando cap height. */
static void m_text_box(int x, int y, int w, int h, const char *s, uint16_t col, int face) {
	int cap = rf_fonts[face].cap;
	m_text(x + (w - m_text_w(s, face)) / 2, y + (h - cap) / 2, s, col, face);
}

/* Recorta con puntos suspensivos si no cabe, en vez de encoger la fuente.
 * Android trunca; no reduce el cuerpo de letra hasta hacerlo ilegible. */
static void m_text_ell(int x, int y, int maxw, const char *s, uint16_t col, int face) {
	if (m_text_w(s, face) <= maxw) { m_text(x, y, s, col, face); return; }
	char buf[128];
	size_t n = strlen(s);
	if (n > 120) n = 120;
	memcpy(buf, s, n); buf[n] = 0;
	while (n > 0) {
		/* Retroceder sin partir un caracter UTF-8 por la mitad. */
		do { n--; } while (n > 0 && (buf[n] & 0xC0) == 0x80);
		buf[n] = 0;
		char t[136];
		snprintf(t, sizeof(t), "%s…", buf);
		if (m_text_w(t, face) <= maxw) { m_text(x, y, t, col, face); return; }
	}
}

/* ---------- Widgets M3 ---------- */

/* Tarjeta tonal: superficie elevada, sin borde brillante. */
static void m_card(int x, int y, int w, int h) {
	m_rrect(x, y, w, h, R_CARD, M_CARD);
}

enum { MB_FILL, MB_TONAL, MB_OUTLINE, MB_TEXT, MB_DANGER, MB_OFF };

/* Boton M3. La pastilla completa es el objetivo tactil. */
static void m_btn(int x, int y, int w, int h, const char *t, int style, int act) {
	uint16_t fill = M_PRIC, fg = M_PRI;
	if (style == MB_FILL) { fill = M_PRI; fg = M_ONPRI; }
	else if (style == MB_OUTLINE) { fill = M_CARD; fg = M_TXT; }
	else if (style == MB_TEXT) { fill = M_CARD; fg = M_PRI; }
	else if (style == MB_DANGER) { fill = M_REDC; fg = M_RED; }
	else if (style == MB_OFF) { fill = M_SURF; fg = M_OUT; }
	bool down = press_live && act != A_NONE && act == press_act;
	if (down) fill = M_CARD2;
	int r = h / 2 < R_BTN ? h / 2 : R_BTN;
	m_rrect(x, y, w, h, r, fill);
	if (style == MB_OUTLINE) m_ring_rect(x, y, w, h, r, M_OUT);
	m_text_box(x, y, w, h, t, fg, F_BTN);
	hit_add(x, y, w, h, act);
}

/* Switch M3: pastilla ancha con pulgar que crece al activarse. */
static void m_switch(int x, int y, bool on, int act) {
	int w = 104, h = 60;
	uint16_t track = on ? M_PRI : M_CARD2;
	uint16_t thumb = on ? M_ONPRI : M_MUT;
	m_rrect(x, y, w, h, h / 2, track);
	if (!on) m_ring_rect(x, y, w, h, h / 2, M_OUT);
	int tr = on ? 22 : 16;
	int tx = on ? x + w - h / 2 : x + h / 2;
	m_circle(tx, y + h / 2, tr, thumb);
	/* El area tactil supera el widget: 104x80 cumple el minimo de 80 px. */
	hit_add(x - 8, y - 10, w + 16, h + 20, act);
}

/* Slider M3 para brillo: track fino, thumb circular grande. */
static void m_slider(int x, int y, int w, int pct, uint16_t col) {
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	int h = 16;
	m_rrect(x, y, w, h, h / 2, M_CARD2);
	int fw = w * pct / 100;
	if (fw > h) m_rrect(x, y, fw, h, h / 2, col);
	m_circle(x + fw, y + h / 2, 14, col);
}

/* Barra de progreso fina, para senal WiFi y rangos. */
static void m_bar(int x, int y, int w, int h, int pct, uint16_t fg) {
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	m_rrect(x, y, w, h, h / 2, M_CARD2);
	int fw = w * pct / 100;
	if (fw > h) m_rrect(x, y, fw, h, h / 2, fg);
}

/* Chip seleccionado / no seleccionado, para modos y presets. */
static void m_chip(int x, int y, int w, int h, const char *t, bool sel, int act) {
	bool down = press_live && act != A_NONE && act == press_act;
	uint16_t fill = sel ? M_PRIC : (down ? M_CARD2 : M_CARD);
	m_rrect(x, y, w, h, R_SMALL, fill);
	if (!sel) m_ring_rect(x, y, w, h, R_SMALL, M_OUT);
	m_text_box(x, y, w, h, t, sel ? M_PRI : M_MUT, F_BTN);
	hit_add(x, y, w, h, act);
}
