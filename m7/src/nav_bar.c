/*
 * Gigaspark OS - Navigation Bar Implementation
 * Android-style OS overlay with Home button
 *
 * The navigation bar is drawn by the OS (not the app) and sits
 * on top of everything. When the user taps the Home button,
 * the OS aborts the running app and returns to the launcher.
 *
 * Note: LVGL is stubbed out until display driver is ready.
 * The logic and flow are complete.
 */

#include "nav_bar.h"
#include "app_runner.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(nav_bar, CONFIG_LOG_DEFAULT_LEVEL);

/* Navigation bar state */
static bool nav_bar_initialized = false;
static bool nav_bar_visible = false;
static bool home_btn_pressed = false;

/* Touch zone for nav bar (entire bottom 40px) */
#define TOUCH_ZONE_Y_START  NAV_BAR_Y_START

/*
 * Process Home button press.
 */
static void home_btn_process(void)
{
	if (!home_btn_pressed) {
		return;
	}

	home_btn_pressed = false;

	LOG_INF("HOME BUTTON PRESSED - Aborting app...");

	/* Force exit the running app */
	app_force_exit();

	/* Hide nav bar */
	nav_bar_hide();

	LOG_INF("App terminated, returning to launcher");
}

void nav_bar_init(void)
{
	LOG_INF("Initializing navigation bar...");
	nav_bar_initialized = true;
	nav_bar_visible = false;
	LOG_INF("Navigation bar initialized (stub mode - no LVGL)");
}

void nav_bar_show(void)
{
	if (!nav_bar_initialized) {
		return;
	}

	nav_bar_visible = true;
	LOG_INF("Navigation bar shown");
}

void nav_bar_hide(void)
{
	if (!nav_bar_initialized) {
		return;
	}

	nav_bar_visible = false;
	LOG_INF("Navigation bar hidden");
}

bool nav_bar_touch_hit(int x, int y)
{
	(void)x;
	return nav_bar_visible && (y >= TOUCH_ZONE_Y_START) && (y < SCREEN_HEIGHT);
}

void nav_bar_touch_handler(int x, int y)
{
	if (!nav_bar_visible) {
		return;
	}

	/*
	 * Home button zone, derived from the screen geometry. The old literals
	 * (190..290) were hardcoded for a 480 px wide screen and would have sat
	 * off-centre on the real 800 px panel.
	 */
	if (x >= NAV_HOME_BTN_X && x < NAV_HOME_BTN_X + NAV_HOME_BTN_W &&
	    y >= NAV_BAR_Y_START + NAV_BAR_PAD &&
	    y < NAV_BAR_Y_START + NAV_BAR_HEIGHT - NAV_BAR_PAD) {
		home_btn_pressed = true;
		home_btn_process();
	}
}

void *nav_bar_get_obj(void)
{
	return NULL;
}
