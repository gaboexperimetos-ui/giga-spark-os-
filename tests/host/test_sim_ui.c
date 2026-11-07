/* Regresion de la interfaz tactil del simulador.
 *
 * El fallo que motivo este test: se dibujaban decenas de botones sin ninguna
 * zona tactil, asi que la UI se veia bien pero no respondia al dedo. Aqui se
 * comprueba el ESTADO (variables del simulador), no solo que cambien pixeles.
 */
#define main gigaspark_sim_entry
#include "main.c"
#undef main

#include <assert.h>

static int failures;

#define CHECK(cond, msg) do { \
	if (!(cond)) { fprintf(stderr, "FALLO: %s\n", msg); failures++; } \
} while (0)

/* Renderiza y devuelve la accion registrada bajo un punto, como haria el dedo. */
static int act_at(int screen, int x, int y)
{
	render_screen(screen, 1000, 100);
	return hit_test(x, y);
}

static bool tap(int screen, int x, int y, int *cur)
{
	render_screen(screen, 1000, 100);
	int act = hit_test(x, y);
	*cur = screen;
	return dispatch(act, cur);
}

/* Centro del primer hit cuya accion coincida. */
static bool center_of(int act, int *x, int *y)
{
	for (int i = 0; i < nhits; i++) {
		if (hits[i].act == act) {
			*x = hits[i].x + hits[i].w / 2;
			*y = hits[i].y + hits[i].h / 2;
			return true;
		}
	}
	return false;
}

static void test_every_screen_has_touch_targets(void)
{
	for (int screen = 0; screen < 7; screen++) {
		render_screen(screen, 1000, 100);
		CHECK(nhits > 0, "pantalla sin zonas tactiles");
		for (int i = 0; i < nhits; i++) {
			int m = hits[i].w < hits[i].h ? hits[i].w : hits[i].h;
			if (m < TOUCH_MIN) {
				fprintf(stderr, "FALLO: pantalla %d accion %d mide %d px (< %d)\n",
					screen, hits[i].act, m, TOUCH_MIN);
				failures++;
			}
			CHECK(hits[i].x >= 0 && hits[i].y >= 0, "hit fuera de pantalla (origen)");
			CHECK(hits[i].x + hits[i].w <= SCR_W, "hit desborda ancho");
			CHECK(hits[i].y + hits[i].h <= SCR_H, "hit desborda alto");
		}
	}
}

static void test_launcher_tiles_open_apps(void)
{
	for (int i = 0; i < LA_N; i++) {
		render_screen(0, 1000, 100);
		int x = 0, y = 0, cur = 0;
		CHECK(center_of(A_APP + i, &x, &y), "mosaico no registrado");
		CHECK(tap(0, x, y, &cur), "toque en mosaico no despacha");
		CHECK(cur == i + 1, "mosaico abrio la app incorrecta");
	}
}

static void test_scope_controls_change_state(void)
{
	int cur = 1, x = 0, y = 0;
	render_screen(1, 1000, 100);

	bool was_run = sc_run;
	CHECK(center_of(A_SC_RUN, &x, &y), "boton run no registrado");
	tap(1, x, y, &cur);
	CHECK(sc_run != was_run, "PAUSA no cambio sc_run");

	int tb = sc_tb;
	render_screen(1, 1000, 100);
	CHECK(center_of(A_SC_TB_P, &x, &y), "boton + no registrado");
	tap(1, x, y, &cur);
	CHECK(sc_tb == tb + 1, "+ no aumento la base de tiempo");

	render_screen(1, 1000, 100);
	CHECK(center_of(A_SC_TB_M, &x, &y), "boton - no registrado");
	tap(1, x, y, &cur);
	CHECK(sc_tb == tb, "- no restauro la base de tiempo");

	bool ch1 = sc_ch1;
	render_screen(1, 1000, 100);
	CHECK(center_of(A_SC_CH1, &x, &y), "boton CH1 no registrado");
	tap(1, x, y, &cur);
	CHECK(sc_ch1 != ch1, "CH1 no conmuta el canal");

	render_screen(1, 1000, 100);
	CHECK(center_of(A_SC_AUTO, &x, &y), "boton AUTO no registrado");
	tap(1, x, y, &cur);
	CHECK(sc_run && sc_ch1 && sc_ch2 && sc_tb == 3, "AUTO no restauro el estado");
}

static void test_psu_controls_change_state(void)
{
	int cur = 2, x = 0, y = 0;
	render_screen(2, 1000, 100);

	int v = ps_v;
	CHECK(center_of(A_PS_V_P, &x, &y), "boton V+ no registrado");
	tap(2, x, y, &cur);
	CHECK(ps_v > v, "V+ no subio el voltaje");

	render_screen(2, 1000, 100);
	CHECK(center_of(A_PS_V_M, &x, &y), "boton V- no registrado");
	tap(2, x, y, &cur);
	CHECK(ps_v == v, "V- no restauro el voltaje");

	int i = ps_i;
	render_screen(2, 1000, 100);
	CHECK(center_of(A_PS_I_P, &x, &y), "boton I+ no registrado");
	tap(2, x, y, &cur);
	CHECK(ps_i > i, "I+ no subio la corriente");

	bool out = ps_out;
	render_screen(2, 1000, 100);
	CHECK(center_of(A_PS_OUT, &x, &y), "boton salida no registrado");
	tap(2, x, y, &cur);
	CHECK(ps_out != out, "el boton grande no conmuta la salida");
}

static void test_psu_limits_clamp(void)
{
	int cur = 2;
	ps_v = 0;
	CHECK(!dispatch(A_PS_V_M, &cur) || ps_v == 0, "voltaje bajo de cero");
	CHECK(ps_v >= 0, "voltaje negativo");
	ps_v = 240;
	dispatch(A_PS_V_P, &cur);
	CHECK(ps_v <= 240, "voltaje paso el maximo");
	ps_i = 50;
	dispatch(A_PS_I_P, &cur);
	CHECK(ps_i <= 50, "corriente paso el maximo");
	ps_v = 50; ps_i = 10;
}

static void test_settings_switches(void)
{
	int cur = 5, x = 0, y = 0;
	render_screen(5, 1000, 100);

	bool wifi = wf_on;
	CHECK(center_of(A_ST_WIFI, &x, &y), "switch WiFi no registrado");
	tap(5, x, y, &cur);
	CHECK(wf_on != wifi, "switch WiFi no conmuta");

	/* Con WiFi apagado, la pantalla de WiFi debe decirlo, no listar redes. */
	render_screen(4, 1000, 100);
	CHECK(hit_test(CX + PAD + 40, CY + PAD + 140) == A_NONE,
	      "con WiFi apagado siguen activos los toques de red");

	render_screen(5, 1000, 100);
	center_of(A_ST_WIFI, &x, &y);
	tap(5, x, y, &cur);
	CHECK(wf_on == wifi, "switch WiFi no vuelve al estado original");

	bool snd = st_sound;
	render_screen(5, 1000, 100);
	CHECK(center_of(A_ST_SOUND, &x, &y), "switch sonido no registrado");
	tap(5, x, y, &cur);
	CHECK(st_sound != snd, "switch sonido no conmuta");

	int br = st_br;
	render_screen(5, 1000, 100);
	CHECK(center_of(A_ST_BR_M, &x, &y), "boton brillo- no registrado");
	tap(5, x, y, &cur);
	CHECK(st_br < br, "brillo- no baja el brillo");
	st_br = br;

	int sl = st_sleep;
	render_screen(5, 1000, 100);
	CHECK(center_of(A_ST_SLEEP, &x, &y), "boton suspension no registrado");
	tap(5, x, y, &cur);
	CHECK(st_sleep != sl, "suspension no cicla opciones");
	st_sleep = sl;
}

static void test_wifi_pagination_and_selection(void)
{
	int cur = 4, x = 0, y = 0;
	wf_on = true; wf_page = 0;
	render_screen(4, 1000, 100);

	/* En la pagina 0 la flecha atras no debe existir como objetivo. */
	CHECK(act_at(4, CX + PAD + 40, CY + PAD + 40) == A_NONE,
	      "flecha atras activa en la primera pagina");

	CHECK(center_of(A_WF_NEXT, &x, &y), "flecha adelante no registrada");
	tap(4, x, y, &cur);
	CHECK(wf_page == 1, "adelante no avanzo de pagina");

	render_screen(4, 1000, 100);
	CHECK(center_of(A_WF_NET + 3, &x, &y), "red de la pagina 2 no registrada");
	tap(4, x, y, &cur);
	CHECK(wf_conn == 3, "tocar una red no la conecta");

	render_screen(4, 1000, 100);
	CHECK(center_of(A_WF_PREV, &x, &y), "flecha atras no registrada en pagina 2");
	tap(4, x, y, &cur);
	CHECK(wf_page == 0, "atras no retrocedio de pagina");
	wf_conn = 0;
}

static void test_rf_modes_and_presets(void)
{
	int cur = 3, x = 0, y = 0;
	render_screen(3, 1000, 100);
	CHECK(center_of(A_RF_MODE + 2, &x, &y), "modo IR no registrado");
	tap(3, x, y, &cur);
	CHECK(fl_mode == 2, "modo no cambia");

	render_screen(3, 1000, 100);
	CHECK(center_of(A_RF_FREQ + 2, &x, &y), "preset 868 no registrado");
	tap(3, x, y, &cur);
	CHECK(rf_freq == 2, "preset no cambia la frecuencia");

	bool scan = rf_scan;
	render_screen(3, 1000, 100);
	CHECK(center_of(A_RF_SCAN, &x, &y), "boton escanear no registrado");
	tap(3, x, y, &cur);
	CHECK(rf_scan != scan, "escanear no conmuta");
	fl_mode = 0; rf_freq = 1; rf_scan = false;
}

static void test_rail_navigation(void)
{
	int cur = 4, x = 0, y = 0;
	render_screen(4, 1000, 100);
	CHECK(center_of(A_HOME, &x, &y), "HOME no registrado");
	tap(4, x, y, &cur);
	CHECK(cur == 0, "HOME no vuelve al launcher");

	render_screen(0, 1000, 100);
	CHECK(act_at(0, RAIL_W / 2, BAR_H + 20) == A_NONE,
	      "ATRAS activo en el launcher, no deberia");

	render_screen(2, 1000, 100);
	CHECK(center_of(A_BACK, &x, &y), "ATRAS no registrado dentro de una app");
	cur = 2;
	tap(2, x, y, &cur);
	CHECK(cur == 0, "ATRAS no vuelve al launcher");
}

static void test_quick_settings_overlay(void)
{
	int cur = 2, x = 0, y = 0;
	render_screen(2, 1000, 100);
	CHECK(center_of(A_MENU, &x, &y), "MENU no registrado");
	tap(2, x, y, &cur);
	CHECK(qs_open, "MENU no abre ajustes rapidos");

	render_screen(cur, 1000, 100);
	CHECK(center_of(A_QS_CLOSE, &x, &y), "cierre del overlay no registrado");
	tap(cur, x, y, &cur);
	CHECK(!qs_open, "el overlay no cierra");
}

static void test_brightness_stays_rgb565(void)
{
	int save = st_br;
	st_br = 20;
	render_screen(0, 1000, 100);
	/* Cualquier uint16_t es RGB565 valido; se verifica que el brillo oscurece. */
	uint32_t dark = 0;
	for (size_t i = 0; i < sizeof(fb) / sizeof(fb[0]); i++) dark += fb[i] >> 11;
	st_br = 100;
	render_screen(0, 1000, 100);
	uint32_t bright = 0;
	for (size_t i = 0; i < sizeof(fb) / sizeof(fb[0]); i++) bright += fb[i] >> 11;
	CHECK(dark < bright, "el brillo no atenua la imagen");
	st_br = save;
}

int main(void)
{
	test_every_screen_has_touch_targets();
	test_launcher_tiles_open_apps();
	test_scope_controls_change_state();
	test_psu_controls_change_state();
	test_psu_limits_clamp();
	test_settings_switches();
	test_wifi_pagination_and_selection();
	test_rf_modes_and_presets();
	test_rail_navigation();
	test_quick_settings_overlay();
	test_brightness_stays_rgb565();
	if (failures) {
		fprintf(stderr, "\n%d comprobaciones fallaron\n", failures);
		return 1;
	}
	puts("UI tactil: todas las comprobaciones pasaron");
	return 0;
}
