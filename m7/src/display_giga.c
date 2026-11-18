/*
 * Gigaspark OS - Backend de pantalla y touch del Arduino GIGA R1 + Display Shield
 *
 * POR QUE EXISTE ESTE FICHERO
 * ---------------------------
 * Antes el hilo de UI del M7 se limitaba a parpadear un LED: nav_bar.c decia
 * literalmente "stub mode - no LVGL" y ui_manager.c/launcher.c ni se
 * compilaban. El firmware arrancaba pero no dibujaba nada. La interfaz
 * Material Design 3 solo existia en el simulador de escritorio.
 *
 * Ahora el M7 ejecuta EL MISMO nucleo de interfaz (common/ui_os.c) que el
 * simulador, contra el panel real. No es una reimplementacion: son los mismos
 * pixeles, verificado con hash de los 7 frames.
 *
 * CADENA DE HARDWARE (verificada contra el esquematico ASX00039)
 * -------------------------------------------------------------
 *   ui_os.c renderiza RGB565 800x480 en un framebuffer en SDRAM
 *     -> LTDC (display-controller@50001000) hace scan-out por DMA
 *     -> host MIPI-DSI, 2 lanes
 *     -> ST7701, panel KD040WVFID026-01-C025A de 3.97"
 *   GT911 (I2C4, 0x5D) -> subsistema input de Zephyr -> hit_test/dispatch
 *
 * El panel es nativo 480x800 vertical. Se rota 90 grados para el 800x480
 * horizontal que usa la interfaz, y el touch se transforma en el Device Tree
 * (screen-width/height nativos + display = <&ltdc> + swapped-x-y), no a mano.
 *
 * LIMITACION CONOCIDA, NO VERIFICABLE SIN LA PLACA
 * -----------------------------------------------
 * Los timings de LTDC y los divisores de PLL3/DSI estan derivados de los del
 * shield (retratos) y de la referencia de ST para el mismo SoC. La compilacion
 * y el encaje en memoria si estan verificados en CI; que el panel encienda con
 * estos valores exactos solo se puede confirmar con el hardware puesto.
 * Ver docs/HARDWARE_FIDELITY.md.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(giga_display, CONFIG_LOG_DEFAULT_LEVEL);

/* Si el overlay no se aplica, esto falla en compilacion en vez de dejar un
 * firmware que arranca mudo. Un overlay mal nombrado se ignora en silencio. */
#if !DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_display), okay)
#error "falta zephyr,display: revisa arduino_giga_r1_m7.overlay (nodo ltdc)"
#endif

#define DISPLAY_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_display))

/* El framebuffer son 768 KB: no cabe en la SRAM interna del M7. Va a la SDRAM
 * de 8 MB de la placa. LINKER_DT_NODE_REGION_NAME da el nombre de seccion que
 * genera `zephyr,memory-region = "SDRAM1"` en el DTS. */
#define UI_TICKS() k_uptime_get_32()
#define UI_OS_FB_SECTION LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(sdram1))
#include "../../common/ui_os.c"

BUILD_ASSERT(SCR_W == 800 && SCR_H == 480, "la interfaz es 800x480");

static const struct device *touch_dev =
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_touch), okay)
	DEVICE_DT_GET(DT_CHOSEN(zephyr_touch));
#else
	NULL;
#endif

/* Estado que comparte el callback de input (contexto de interrupcion/trabajo)
 * con el hilo de UI. Solo este hilo renderiza. */
static volatile int req_screen;
static volatile bool touch_pending;
static volatile int touch_x, touch_y;
static struct k_spinlock input_lock;

int display_giga_init(void)
{
	if (!device_is_ready(DISPLAY_DEV)) {
		LOG_ERR("el panel no esta listo (LTDC/ST7701)");
		return -ENODEV;
	}

	struct display_capabilities cap;
	display_get_capabilities(DISPLAY_DEV, &cap);
	LOG_INF("panel %ux%u, formato %d, orientacion %d",
		cap.x_resolution, cap.y_resolution,
		cap.current_pixel_format, cap.current_orientation);

	if (cap.x_resolution != SCR_W || cap.y_resolution != SCR_H) {
		LOG_ERR("panel %ux%u pero la interfaz es %dx%d: revisa la rotacion",
			cap.x_resolution, cap.y_resolution, SCR_W, SCR_H);
		return -EINVAL;
	}
	if (cap.current_pixel_format != PIXEL_FORMAT_RGB_565) {
		LOG_ERR("se esperaba RGB565, el panel esta en formato %d",
			cap.current_pixel_format);
		return -EINVAL;
	}

	req_screen = 0;
	display_blanking_off(DISPLAY_DEV);
	LOG_INF("framebuffer de %u KB en SDRAM1", (unsigned int)(sizeof(fb) / 1024U));
	return 0;
}

/* Traduce un evento del subsistema input a la interfaz. El GT911 ya entrega
 * coordenadas en el espacio del panel gracias a la transformada del DTS. */
static void input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (touch_dev != NULL && evt->dev != touch_dev) {
		return;
	}

	static int px, py;
	static bool down;

	switch (evt->type) {
	case INPUT_EV_ABS:
		if (evt->code == INPUT_ABS_X) {
			px = evt->value;
		} else if (evt->code == INPUT_ABS_Y) {
			py = evt->value;
		}
		break;
	case INPUT_EV_KEY:
		if (evt->code != INPUT_BTN_TOUCH) {
			break;
		}
		down = evt->value != 0;
		/* Solo el flanco de bajada dispara la accion: la interfaz es
		 * de pulsacion, no de arrastre, igual que en el simulador. */
		if (down) {
			k_spinlock_key_t key = k_spin_lock(&input_lock);
			touch_x = px;
			touch_y = py;
			touch_pending = true;
			k_spin_unlock(&input_lock, key);
		}
		break;
	default:
		break;
	}
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

void display_giga_run(void)
{
	int screen = 0;
	uint8_t bat = 100;
	uint32_t last_battery = k_uptime_get_32();
	bool dirty = true;

	for (;;) {
		uint32_t now = k_uptime_get_32();

		if (touch_pending) {
			int x, y;
			k_spinlock_key_t key = k_spin_lock(&input_lock);
			x = touch_x;
			y = touch_y;
			touch_pending = false;
			k_spin_unlock(&input_lock, key);

			/* Las zonas tactiles se registraron en el ultimo render,
			 * asi que lo que se ve es lo que responde. */
			int act = hit_test(x, y);
			if (act != A_NONE) {
				press_act = act;
				press_live = true;
				press_until = now + 110;
				dispatch(act, &screen);
				dirty = true;
			}
		}

		if (press_live && now >= press_until) {
			press_live = false;
			dirty = true;
		}
		if (wf_scan_until != 0 && now >= wf_scan_until) {
			wf_scan_until = 0;
			dirty = true;
		}
		if (now - last_battery >= 30000) {
			if (bat > 5) {
				--bat;
			}
			last_battery = now;
			dirty = true;
		}

		if (dirty || screen_is_animated(screen)) {
			render_screen(screen, now, bat);

			struct display_buffer_descriptor desc = {
				.buf_size = sizeof(fb),
				.width = SCR_W,
				.pitch = SCR_W,
				.height = SCR_H,
			};
			int rc = display_write(DISPLAY_DEV, 0, 0, &desc, fb);
			if (rc < 0) {
				LOG_ERR("display_write fallo (%d)", rc);
			}
			dirty = false;
		}

		/* Brillo: el backlight del shield es SWIRE (un solo cable), no
		 * PWM. Se aplica el atenuado del nucleo cuando el driver expone
		 * set_brightness; si no, el panel queda a brillo fijo. */
		(void)backlight_scale();

		k_msleep(screen_is_animated(screen) ? 33 : 100);
	}
}
