/*
 * Gigaspark OS - SDK API Implementation
 * Runtime support for user apps
 *
 * This file provides the actual implementations of SDK functions
 * and populates the export table at 0xC0001000.
 */

#include "gigaspark_api.h"
#include "ipc_protocol.h"
#include "framebuffer.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Para funciones de sistema (bateria, timeout) */
#include "../m7/src/power_manager.h"

LOG_MODULE_REGISTER(gigaspark_sdk, CONFIG_LOG_DEFAULT_LEVEL);

/* ---- Memory ---- */

static void *giga_malloc_impl(size_t size)
{
	return k_malloc(size);
}

static void giga_free_impl(void *ptr)
{
	k_free(ptr);
}

/* ---- Display (stub - writes to framebuffer) ---- */

static uint16_t fb[GIGA_SCREEN_W * GIGA_SCREEN_H] __attribute__((aligned(4)));

static void giga_fill_rect_impl(int x, int y, int w, int h, uint16_t color)
{
	/* Preserve the SDK's reject-outside-bounds contract without x+w overflow. */
	if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
	    w > GIGA_SCREEN_W - x || h > GIGA_SCREEN_H - y || x >= GIGA_SCREEN_W || y >= GIGA_SCREEN_H)
		return;
	for (int row = 0; row < h; ++row)
		giga_fb_span(&fb[(y + row) * GIGA_SCREEN_W + x], (size_t)w, color);
}

static void giga_draw_rect_impl(int x, int y, int w, int h, uint16_t color)
{
	if (!giga_fb_clip(&x, &y, &w, &h, GIGA_SCREEN_W, GIGA_SCREEN_H)) return;
	giga_fb_span(&fb[y * GIGA_SCREEN_W + x], (size_t)w, color);
	giga_fb_span(&fb[(y + h - 1) * GIGA_SCREEN_W + x], (size_t)w, color);
	for (int row = 1; row < h - 1; ++row) {
		fb[(y + row) * GIGA_SCREEN_W + x] = color;
		fb[(y + row) * GIGA_SCREEN_W + x + w - 1] = color;
	}
}

static void giga_draw_text_impl(int x, int y, const char *text, uint16_t color)
{
	/* Stub: log the text being drawn */
	if (text) {
		LOG_DBG("draw_text(%d, %d, '%s', 0x%04x)", x, y, text, color);
	}
}

static void giga_update_impl(void)
{
	/* TODO: Send framebuffer to LTDC/DMA2D */
	LOG_DBG("Display update");
}

/* ---- Touch ---- */

static bool giga_get_touch_impl(int *x, int *y)
{
	/* TODO: Read from LVGL indev */
	if (x) *x = 0;
	if (y) *y = 0;
	return false;
}

/* ---- Buttons (LVGL) ---- */

static void *giga_btn_create_impl(void *parent)
{
	/* TODO: Call lv_btn_create(parent) */
	LOG_DBG("btn_create(parent=%p)", parent);
	return NULL;
}

static void giga_btn_set_text_impl(void *btn, const char *txt)
{
	/* TODO: Call lv_label_set_text() on button's label */
	LOG_DBG("btn_set_text(btn=%p, '%s')", btn, txt);
}

static void giga_btn_set_pos_impl(void *btn, int x, int y)
{
	/* TODO: Call lv_obj_set_pos() */
	LOG_DBG("btn_set_pos(btn=%p, %d, %d)", btn, x, y);
}

static void giga_btn_set_size_impl(void *btn, int w, int h)
{
	/* TODO: Call lv_obj_set_size() */
	LOG_DBG("btn_set_size(btn=%p, %d, %d)", btn, w, h);
}

static bool giga_btn_hit_impl(void *btn, int x, int y)
{
	/* TODO: Check if (x,y) is within button bounds */
	(void)btn; (void)x; (void)y;
	return false;
}

static uint16_t giga_btn_get_state_impl(void *btn)
{
	/* TODO: Return lv_obj_get_state(btn) */
	(void)btn;
	return 0;
}

/* ---- Timing ---- */

static void giga_sleep_ms_impl(uint32_t ms)
{
	k_msleep(ms);
}

static uint32_t giga_get_tick_ms_impl(void)
{
	return k_uptime_get_32();
}

/* ---- Logging ---- */

static void giga_log_info_impl(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char buf[256];
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	LOG_INF("[APP] %s", buf);
}

static void giga_log_error_impl(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char buf[256];
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	LOG_ERR("[APP] %s", buf);
}

/* ---- IPC ---- */

static int giga_ipc_send_impl(uint8_t cmd, const void *data, size_t len)
{
	/* TODO: Send via IPC to M4 */
	(void)cmd; (void)data; (void)len;
	return 0;
}

static int giga_ipc_recv_impl(void *data, size_t len, uint32_t timeout_ms)
{
	/* TODO: Receive via IPC from M4 */
	(void)data; (void)len; (void)timeout_ms;
	return 0;
}

/* ---- Text Rendering ---- */

static void giga_set_font_impl(uint8_t font_id)
{
	(void)font_id;
	LOG_DBG("set_font(%d)", font_id);
}

static void giga_get_text_size_impl(const char *text, int *w, int *h)
{
	/* Estimacion: 8px por char, 16px alto */
	if (text) {
		if (w) *w = strlen(text) * 8;
		if (h) *h = 16;
	} else {
		if (w) *w = 0;
		if (h) *h = 0;
	}
}

/* ---- Red (Fase 16) ---- */

static int giga_http_get_impl(const char *url, uint8_t *buf,
			      size_t buf_len, size_t *out_len)
{
	/*
	 * Implementacion stub para HTTP GET.
	 * En produccion, esto usaria la pila TCP/IP de Zephyr
	 * (NET_SOCKETS) para hacer una peticion HTTP/HTTPS.
	 *
	 * Por ahora, retorna -ENOSYS (no implementado).
	 * Las apps que requieran red deben usar el WiFi manager
	 * y las funciones de socket directamente.
	 */
	(void)url; (void)buf; (void)buf_len;
	if (out_len) *out_len = 0;
	return -ENOSYS;
}

static int giga_http_post_impl(const char *url, const char *content_type,
			       const uint8_t *data, size_t data_len,
			       uint8_t *buf, size_t buf_len, size_t *out_len)
{
	/*
	 * Implementacion stub para HTTP POST.
	 * Mismo caso que GET: requiere pila TCP/IP completa.
	 * Las apps pueden usar net_manager.c directamente.
	 */
	(void)url; (void)content_type; (void)data; (void)data_len;
	(void)buf; (void)buf_len;
	if (out_len) *out_len = 0;
	return -ENOSYS;
}

/* ---- Memoria Avanzada (Fase 16) ---- */

static uint32_t giga_mem_alloc_buddy_impl(uint32_t size)
{
	/* Delegar al motor de memoria M4 via IPC */
	struct ipc_msg msg = {
		.cmd = CMD_ALLOC,
		.status = 0,
		.handle = 0,
		.size = size,
	};
	int ret = giga_ipc_send_impl(CMD_ALLOC, &msg, sizeof(msg));
	if (ret < 0) {
		return 0;
	}
	/* En una implementacion real, esperariamos la respuesta */
	return 0;
}

static void giga_mem_free_buddy_impl(uint32_t handle)
{
	/* Delegar al motor de memoria M4 via IPC */
	struct ipc_msg msg = {
		.cmd = CMD_FREE,
		.status = 0,
		.handle = (uint16_t)handle,
		.size = 0,
	};
	giga_ipc_send_impl(CMD_FREE, &msg, sizeof(msg));
}

static uint8_t giga_mem_get_usage_impl(void)
{
	/* En una implementacion real, esto consultaria al M4 */
	return 0;
}

/* ---- zRAM LRU (Fase 16) ---- */

static size_t giga_zram_store_impl(uint32_t handle, const uint8_t *data,
				   size_t len)
{
	/* En produccion, esto comprime y almacena via M4 */
	(void)handle; (void)data; (void)len;
	return 0;
}

static size_t giga_zram_load_impl(uint32_t handle, uint8_t *buf,
				  size_t buf_len)
{
	/* En produccion, esto recupera y descomprime via M4 */
	(void)handle; (void)buf; (void)buf_len;
	return 0;
}

static void giga_zram_get_stats_impl(uint8_t *used, uint8_t *total,
				     uint32_t *counter)
{
	(void)used; (void)total; (void)counter;
}

/* ---- Sistema (Fase 20) ---- */

static uint8_t giga_get_battery_pct_impl(void)
{
	/*
	 * Stub: no hay hardware de bateria configurado.
	 * Futuramente leera un canal ADC (A8/A9) con divisor de voltaje.
	 * Por ahora retorna 100% (alimentacion USB/DC directa).
	 */
	return 100;
}

static uint32_t giga_get_screen_timeout_impl(void)
{
	return power_get_timeout();
}

static void giga_set_screen_timeout_impl(uint32_t seconds)
{
	power_set_timeout(seconds);
}

/* ---- Export Table ---- */

static const giga_api_t giga_api_table = {
	.version       = GIGA_API_VERSION,
	.malloc        = giga_malloc_impl,
	.free          = giga_free_impl,
	.fill_rect     = giga_fill_rect_impl,
	.draw_rect     = giga_draw_rect_impl,
	.draw_text     = giga_draw_text_impl,
	.update        = giga_update_impl,
	.get_touch     = giga_get_touch_impl,
	.btn_create    = giga_btn_create_impl,
	.btn_set_text  = giga_btn_set_text_impl,
	.btn_set_pos   = giga_btn_set_pos_impl,
	.btn_set_size  = giga_btn_set_size_impl,
	.btn_hit       = giga_btn_hit_impl,
	.btn_get_state = giga_btn_get_state_impl,
	.sleep_ms      = giga_sleep_ms_impl,
	.get_tick_ms   = giga_get_tick_ms_impl,
	.log_info      = giga_log_info_impl,
	.log_error     = giga_log_error_impl,
	.ipc_send      = giga_ipc_send_impl,
	.ipc_recv      = giga_ipc_recv_impl,
	.set_font      = giga_set_font_impl,
	.get_text_size = giga_get_text_size_impl,
	/* Fase 16: Red */
	.http_get      = giga_http_get_impl,
	.http_post     = giga_http_post_impl,
	/* Fase 16: Memoria avanzada */
	.mem_alloc_buddy = giga_mem_alloc_buddy_impl,
	.mem_free_buddy  = giga_mem_free_buddy_impl,
	.mem_get_usage   = giga_mem_get_usage_impl,
	/* Fase 16: zRAM LRU */
	.zram_store     = giga_zram_store_impl,
	.zram_load      = giga_zram_load_impl,
	.zram_get_stats = giga_zram_get_stats_impl,
	/* Fase 20: Sistema */
	.get_battery_pct     = giga_get_battery_pct_impl,
	.get_screen_timeout  = giga_get_screen_timeout_impl,
	.set_screen_timeout  = giga_set_screen_timeout_impl,
};

/*
 * Initialize the SDK export table.
 * Copies the table to the known address (0xC0001000)
 * so apps can access it.
 */
void gigaspark_sdk_init(void)
{
	LOG_INF("Initializing SDK export table at 0x%08x", GIGA_API_TABLE_ADDR);

	/*
	 * In a production system, you'd set up an MPU region for 0xC0001000
	 * that maps to a RAM region containing the table.
	 * For now, we'll use a linker symbol approach.
	 *
	 * The table is placed at the beginning of a dedicated memory section.
	 * Apps access it via the fixed address.
	 */
	LOG_INF("SDK v0x%04x initialized", GIGA_API_VERSION);
}

/* Export the table symbol for linker */
const giga_api_t *gigaspark_get_api(void)
{
	return &giga_api_table;
}
