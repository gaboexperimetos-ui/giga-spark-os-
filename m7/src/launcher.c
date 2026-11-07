/*
 * Gigaspark OS - Launcher Implementation
 * Application launcher menu for LVGL v9 display
 *
 * Requests the app list from M4 via IPC and creates a visual
 * button menu on the 480x272 virtual display.
 *
 * NOTE: 480x272 is the current virtual canvas, not the real Giga Display
 * Shield. That panel is 480x800 native (ST7701 over MIPI DSI, not LTDC).
 * Migration pending; see docs/HARDWARE_FIDELITY.md.
 */

#include "launcher.h"
#include "ipc_protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <string.h>

LOG_MODULE_REGISTER(launcher, CONFIG_LOG_DEFAULT_LEVEL);

/* IPC endpoint reference */
static struct ipc_ept *launcher_ept;

/* App list from M4 */
static struct app_info app_list[APP_MAX_COUNT];
static int app_count = 0;

/* LVGL objects */
static lv_obj_t *main_screen;
static lv_obj_t *status_label;

/* Response synchronization */
static K_SEM_DEFINE(resp_sem, 0, 1);
static struct ipc_msg last_response;

/* Button click event handler */
static void btn_click_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *btn = lv_event_get_target(e);

	if (code != LV_EVENT_CLICKED) {
		return;
	}

	/* Get app id from button user data */
	int btn_id = (int)(intptr_t)lv_event_get_user_data(e);

	if (btn_id < 0 || btn_id >= app_count) {
		LOG_WRN("Invalid button id: %d", btn_id);
		return;
	}

	LOG_INF("App[%d] selected: %s", btn_id, app_list[btn_id].name);

	/* Update status label */
	lv_label_set_text_fmt(status_label, "Loading: %s...", app_list[btn_id].name);

	/* Send CMD_LOAD_APP to M4 */
	struct ipc_msg cmd = {
		.cmd = CMD_LOAD_APP,
		.handle = app_list[btn_id].id,
		.size = 0,
		.status = 0,
	};

	int ret = ipc_service_send(launcher_ept, &cmd, sizeof(cmd));
	if (ret < 0) {
		LOG_ERR("Failed to send CMD_LOAD_APP: %d", ret);
		lv_label_set_text(status_label, "Load failed!");
		return;
	}

	/* Wait for response */
	ret = k_sem_take(&resp_sem, K_MSEC(2000));
	if (ret < 0) {
		LOG_ERR("CMD_LOAD_APP response timeout");
		lv_label_set_text(status_label, "Load timeout!");
		return;
	}

	const struct ipc_msg *resp = (const struct ipc_msg *)&last_response;
	if (resp->status == STATUS_OK) {
		lv_label_set_text_fmt(status_label,
			"Loaded: %s (handle=0x%04x, %u bytes)",
			app_list[btn_id].name, resp->handle, resp->size);
		LOG_INF("App loaded: handle=0x%04x size=%u",
			resp->handle, resp->size);
	} else {
		lv_label_set_text_fmt(status_label,
			"Load error: %d", resp->status);
		LOG_ERR("App load failed: status=%d", resp->status);
	}
}

int launcher_init(struct ipc_ept *ept)
{
	int ret;

	launcher_ept = ept;

	LOG_INF("Launcher: requesting app list from M4...");

	/* Send CMD_GET_APP_LIST to M4 */
	struct ipc_msg cmd = {
		.cmd = CMD_GET_APP_LIST,
		.handle = 0,
		.size = 0,
		.status = 0,
	};

	ret = ipc_service_send(ept, &cmd, sizeof(cmd));
	if (ret < 0) {
		LOG_ERR("Failed to send CMD_GET_APP_LIST: %d", ret);
		return ret;
	}

	/* Wait for response */
	ret = k_sem_take(&resp_sem, K_MSEC(2000));
	if (ret < 0) {
		LOG_ERR("CMD_GET_APP_LIST response timeout");
		return -ETIMEDOUT;
	}

	const struct ipc_msg *resp = (const struct ipc_msg *)&last_response;
	if (resp->status != STATUS_OK) {
		LOG_WRN("No apps available (status=%d)", resp->status);
		app_count = 0;
	} else {
		app_count = resp->size;
		LOG_INF("M4 reports %d app(s)", app_count);
	}

	/* Build the LVGL UI */
	main_screen = lv_scr_act();
	lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x1a1a2e), 0);

	/* Title */
	lv_obj_t *title = lv_label_create(main_screen);
	lv_label_set_text(title, "Gigaspark OS - Launcher");
	lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(title, lv_color_hex(0xe94560), 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

	/* Create buttons for each app */
	if (app_count > 0) {
		for (int i = 0; i < app_count && i < APP_MAX_COUNT; i++) {
			lv_obj_t *btn = lv_btn_create(main_screen);
			lv_obj_set_size(btn, 420, 36);
			lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 50 + i * 42);

			/* Button style */
			lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
			lv_obj_set_style_border_color(btn, lv_color_hex(0x0f3460), 0);
			lv_obj_set_style_border_width(btn, 2, 0);
			lv_obj_set_style_radius(btn, 8, 0);

			/* Button label */
			lv_obj_t *label = lv_label_create(btn);
			lv_label_set_text(label, app_list[i].name);
			lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
			lv_obj_center(label);

			/* Click event */
			lv_obj_add_event_cb(btn, btn_click_cb, LV_EVENT_CLICKED,
					    (void *)(intptr_t)i);
		}
	} else {
		lv_obj_t *no_apps = lv_label_create(main_screen);
		lv_label_set_text(no_apps,
			"No apps found on SD card.\n"
			"Insert SD with /apps/*.bin files.");
		lv_obj_set_style_text_color(no_apps, lv_color_hex(0xaaaaaa), 0);
		lv_obj_align(no_apps, LV_ALIGN_CENTER, 0, 0);
	}

	/* Status label at bottom */
	status_label = lv_label_create(main_screen);
	lv_label_set_text(status_label, "Ready");
	lv_obj_set_style_text_color(status_label, lv_color_hex(0x53d769), 0);
	lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -10);

	LOG_INF("Launcher UI created");
	return 0;
}

void launcher_handle_response(const void *resp_data)
{
	if (resp_data == NULL) {
		return;
	}

	memcpy(&last_response, resp_data, sizeof(last_response));
	k_sem_give(&resp_sem);
}
