/*
 * Gigaspark OS - UI Manager Implementation
 * LVGL v9 initialization and display/touch management
 *
 * Initializes LVGL v9, creates display and touch input,
 * and runs a dedicated thread for lv_timer_handler() at 5ms intervals.
 */

#include "ui_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_REGISTER(ui_manager, CONFIG_LOG_DEFAULT_LEVEL);

/* Dedicated thread for LVGL handler */
#define LVGL_STACK_SIZE 4096
#define LVGL_PRIORITY   5

K_THREAD_STACK_DEFINE(lvgl_stack, LVGL_STACK_SIZE);
static struct k_thread lvgl_thread_data;

/*
 * Display dimensions.
 * Virtual canvas only: the real Giga Display Shield is 480x800 native
 * (800x480 landscape). Migration pending; see docs/HARDWARE_FIDELITY.md.
 */
#define DISPLAY_WIDTH   480
#define DISPLAY_HEIGHT  272

/* LVGL display and input device references */
static lv_display_t *display;
static lv_indev_t *indev;

/*
 * LVGL display flush callback.
 */
static void display_flush_cb(lv_display_t *disp, const lv_area_t *area,
			     uint8_t *color_p)
{
	/*
	 * In a real implementation, this would hand the pixels to the display
	 * driver. Note the Giga Display Shield panel (ST7701) hangs off MIPI
	 * DSI, not LTDC, so the flush path differs from a parallel-RGB panel.
	 * For now, we just mark the flush as complete.
	 */
	lv_display_flush_ready(disp);
}

/*
 * LVGL touchpad read callback.
 */
static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	/*
	 * In a real implementation, this would read from the Goodix GT911
	 * I2C touch controller via Zephyr's input subsystem. The Giga Display
	 * Shield uses a GT911 (up to 5 touch points), not an FT6206.
	 * See docs/HARDWARE_FIDELITY.md.
	 */
	data->state = LV_INDEV_STATE_RELEASED;
}

/*
 * LVGL handler thread.
 */
static void lvgl_handler_thread(void *p1, void *p2, void *p3)
{
	LOG_INF("LVGL handler thread started");

	while (1) {
		lv_timer_handler();
		k_msleep(5);
	}
}

int ui_init(void)
{
	LOG_INF("Initializing LVGL v9...");

	/* Initialize LVGL */
	lv_init();

	/* Create display */
	display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
	lv_display_set_flush_cb(display, display_flush_cb);

	/* Set display theme */
	lv_theme_t *th = lv_theme_default_init(
		lv_disp_get_default(),
		lv_palette_main(LV_PALETTE_BLUE),
		lv_palette_main(LV_PALETTE_CYAN),
		1,   /* dark mode */
		lv_font_get_default()
	);
	lv_disp_set_theme(lv_disp_get_default(), th);

	/* Create touch input device */
	indev = lv_indev_create();
	lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
	lv_indev_set_read_cb(indev, touchpad_read_cb);

	/* Start LVGL handler thread */
	k_thread_create(&lvgl_thread_data, lvgl_stack,
			LVGL_STACK_SIZE,
			lvgl_handler_thread,
			NULL, NULL, NULL,
			LVGL_PRIORITY, 0, K_NO_WAIT);

	k_thread_name_set(&lvgl_thread_data, "lvgl_handler");

	LOG_INF("LVGL v9 initialized, display=%dx%d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
	return 0;
}

void *ui_get_display(void)
{
	return (void *)display;
}
