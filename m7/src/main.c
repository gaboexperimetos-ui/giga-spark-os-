/*
 * Gigaspark OS - M7 Primary Core
 * STM32H747XI Cortex-M7 @ 480MHz
 *
 * Architecture: 2 Dedicated Threads (Strict Multiplexation)
 *
 * Thread 1 - ui_launcher (Priority: Low):
 *   EXCLUSIVELY handles LVGL rendering, touch input, and the app menu.
 *   Runs lv_timer_handler() in a tight loop at 5ms intervals.
 *   Manages the navigation bar (Home button).
 *
 * Thread 2 - app_runner (Priority: High):
 *   EXCLUSIVELY executes user apps (app_main()) and handles the SDK.
 *   Communicates with M4 via IPC for memory and filesystem ops.
 *   Can be aborted by ui_launcher when Home is pressed.
 *
 * Communication: main() initializes everything, then threads take over.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ipc/ipc_service.h>
#include <zephyr/logging/log.h>

#include "ipc_config.h"
#include "ipc_protocol.h"
#include "fault_manager.h"
#include "app_runner.h"
#include "nav_bar.h"
#include "power_manager.h"
#include "net_manager.h"
#include "app_downloader.h"
#include "display_giga.h"

LOG_MODULE_REGISTER(gigaspark_m7, CONFIG_LOG_DEFAULT_LEVEL);

/* SDK init */
extern void gigaspark_sdk_init(void);

/* Green LED = PJ13 (alias led1) */
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* ============================================================
 * IPC Layer (shared by both threads)
 * ============================================================ */

static struct ipc_ept ept;
static volatile bool ept_ready;
static K_SEM_DEFINE(bound_sem, 0, 1);
static K_SEM_DEFINE(data_sem, 0, 1);

/* Last response from M4 */
static struct ipc_msg last_resp;

/* Shared buffer for file writes */
uint8_t g_file_write_buf[FILE_WRITE_CHUNK_MAX];

static void ept_bound(void *priv)
{
	(void)priv;
	ept_ready = true;
	k_sem_give(&bound_sem);
	LOG_INF("Endpoint bound to M4");
}

static void ept_received(const void *data, size_t len, void *priv)
{
	(void)priv;
	if (len < sizeof(struct ipc_msg)) {
		return;
	}

	memcpy(&last_resp, data, sizeof(last_resp));
	k_sem_give(&data_sem);
}

static struct ipc_ept_cfg ept_cfg = {
	.name = IPC_ENDPOINT_NAME,
	.cb = {
		.bound    = ept_bound,
		.received = ept_received,
	},
};

/* Send a command to M4 and wait for response (blocking) */
int send_ipc_cmd(struct ipc_msg *cmd, struct ipc_msg *resp)
{
	int ret;

	ret = ipc_service_send(&ept, cmd, sizeof(*cmd));
	if (ret < 0) {
		LOG_ERR("Send failed: %d", ret);
		return ret;
	}

	k_sem_take(&data_sem, K_FOREVER);
	memcpy(resp, &last_resp, sizeof(*resp));
	return 0;
}

/* ============================================================
 * UI Launcher Thread (Low Priority)
 * EXCLUSIVELY handles LVGL rendering, touch input, and navigation bar.
 * If an app enters an infinite loop, this thread keeps running.
 * ============================================================ */

static void ui_launcher_thread(void *p1, void *p2, void *p3)
{
	(void)p1; (void)p2; (void)p3;
	LOG_INF("UI Launcher thread starting...");

	/* Initialize navigation bar */
	nav_bar_init();

	/* Initialize power manager */
	power_manager_init();

	/* Pantalla real del Arduino GIGA Display Shield.
	 *
	 * Antes esto era un TODO y el hilo se limitaba a parpadear un LED: el
	 * firmware arrancaba pero no dibujaba nada, y la interfaz Material solo
	 * existia en el simulador. Ahora corre el mismo nucleo (common/ui_os.c)
	 * sobre el panel ST7701 por LTDC, con touch GT911.
	 *
	 * Si el panel no esta listo se registra el error y se mantiene el LED
	 * como ultimo recurso: el resto del OS (IPC, apps, red) sigue vivo. */
	if (display_giga_init() == 0) {
		display_giga_run();   /* no retorna */
	}

	LOG_ERR("panel no disponible: la interfaz no se puede dibujar");
	for (;;) {
		gpio_pin_toggle_dt(&led_green);
		k_msleep(500);
	}
}

/* Static thread: UI launcher, low priority (PRIORITY=14) */
K_THREAD_DEFINE(ui_launcher_tid, 8192,
		ui_launcher_thread, NULL, NULL, NULL,
		14, 0, 0);

/* ============================================================
 * App Runner Thread (High Priority)
 * EXCLUSIVELY executes user apps and handles SDK operations.
 * Communicates with M4 via IPC.
 * ============================================================ */

static void app_runner_thread(void *p1, void *p2, void *p3)
{
	(void)p1; (void)p2; (void)p3;
	int ret;

	LOG_INF("App Runner thread starting...");

	/* Set up thread reference for force exit */
	app_runner_set_thread(k_current_get());

	/* Wait for IPC to be ready */
	k_sem_take(&bound_sem, K_FOREVER);

	/* === WiFi Connection (stub) === */
	LOG_INF("=== Connecting to WiFi ===");
	ret = net_connect_wifi("Gigaspark-AP", "gigaspark123");
	if (ret < 0) {
		LOG_WRN("WiFi connection failed: %d", ret);
	} else {
		ret = net_wait_ip(10000);
		if (ret < 0) {
			LOG_WRN("Failed to get IP: %d", ret);
		} else {
			char ip[16];
			net_get_ip_str(ip, sizeof(ip));
			LOG_INF("Connected! IP: %s", ip);

			/* Try to download an app */
			LOG_INF("=== Downloading app from server ===");
			ret = download_app_from_url(
				"192.168.1.100", 80,
				"/apps/clicker.bin", "clicker.bin"
			);
			if (ret < 0) {
				LOG_WRN("Download failed: %d", ret);
			} else {
				LOG_INF("App downloaded successfully!");
			}
		}
	}

	/* === App Manager === */
	LOG_INF("=== Gigaspark OS App Manager ===");

	/* Get app list from M4 */
	struct ipc_msg list_cmd = {
		.cmd = CMD_GET_APP_LIST,
		.handle = 0,
		.size = 0,
		.status = 0,
	};
	struct ipc_msg resp;

	ret = send_ipc_cmd(&list_cmd, &resp);
	if (ret < 0) {
		LOG_ERR("Failed to get app list: %d", ret);
	} else if (resp.status == STATUS_OK) {
		int app_count = resp.size;
		LOG_INF("Found %d app(s) on SD card", app_count);

		/* Load and execute first app if available */
		if (app_count > 0) {
			LOG_INF("--- Loading app 0 ---");

			struct ipc_msg load_cmd = {
				.cmd = CMD_LOAD_APP,
				.handle = 0,
				.size = 0,
				.status = 0,
			};

			ret = send_ipc_cmd(&load_cmd, &resp);
			if (ret == 0 && resp.status == STATUS_OK) {
				LOG_INF("App loaded: handle=0x%04x, size=%u bytes",
					resp.handle, resp.size);

				/* Show navigation bar */
				nav_bar_show();

				/* Execute app with fault recovery */
				LOG_INF("Executing app with fault recovery...");
				int app_result = app_runner_execute(
					(const uint8_t *)(uintptr_t)resp.handle,
					resp.size,
					resp.handle
				);

				/* Hide navigation bar when app exits */
				nav_bar_hide();

				if (app_result == APP_RESULT_FAULT) {
					LOG_WRN("App faulted - recovered, returning to launcher");
				} else if (app_result == APP_RESULT_OK) {
					LOG_INF("App exited normally");
				} else {
					LOG_ERR("App execution error: %d", app_result);
				}
			}
		}
	}

	LOG_INF("App Runner: entering idle loop");

	/* App runner thread idle - waits for next app to execute */
	while (1) {
		k_msleep(1000);
	}
}

/* Static thread: App runner, high priority (PRIORITY=5) */
K_THREAD_DEFINE(app_runner_tid, 8192,
		app_runner_thread, NULL, NULL, NULL,
		5, 0, 0);

/* ============================================================
 * Main (Initialization Only)
 * Initializes hardware and subsystems, then threads take over.
 * ============================================================ */

int main(void)
{
	int ret;

	LOG_INF("====================================================");
	LOG_INF(" GIGASPARK OS v0.2.0");
	LOG_INF(" M7 @ 480MHz | M4 @ 240MHz");
	LOG_INF(" Architecture: 2 cores x 2 threads = 4 threads");
	LOG_INF("====================================================");
	LOG_INF("M7 Primary Core starting...");

	/* Initialize GPIO for green LED */
	if (!gpio_is_ready_dt(&led_green)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure LED: %d", ret);
		return ret;
	}

	/* Initialize fault manager */
	fault_manager_init();
	LOG_INF("Fault manager: OK");

	/* Initialize SDK */
	gigaspark_sdk_init();
	LOG_INF("SDK export table: OK");

	/* Initialize network */
	ret = net_manager_init();
	if (ret < 0) {
		LOG_WRN("Network init failed: %d", ret);
	} else {
		LOG_INF("Network manager: OK");
	}

	/* Initialize IPC */
	const struct device *ipc_instance = DEVICE_DT_GET(DT_NODELABEL(ipc0));

	ret = ipc_service_open_instance(ipc_instance);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to open IPC instance: %d", ret);
		return ret;
	}

	ret = ipc_service_register_endpoint(ipc_instance, &ept, &ept_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to register endpoint: %d", ret);
		return ret;
	}

	LOG_INF("IPC endpoint: OK");
	LOG_INF("====================================================");
	LOG_INF(" M7 Threads:");
	LOG_INF("   ui_launcher: tid=%d, prio=14 (LOW)", (int)ui_launcher_tid);
	LOG_INF("   app_runner:  tid=%d, prio=5 (HIGH)", (int)app_runner_tid);
	LOG_INF("====================================================");

	/* main() thread sleeps forever - threads do all the work */
	return 0;
}
