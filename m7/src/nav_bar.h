/*
 * Gigaspark OS - Navigation Bar Header
 * Android-style OS overlay with Home button
 */

#ifndef GIGASPARK_NAV_BAR_H
#define GIGASPARK_NAV_BAR_H

#include <stdbool.h>

/*
 * Screen dimensions: landscape mode of the real Giga Display Shield
 * (ASX00039). The panel is 480x800 native, ST7701 over MIPI DSI, with a
 * Goodix GT911 capacitive touch controller. See docs/HARDWARE_FIDELITY.md.
 *
 * These match SCR_W/SCR_H in simulator/main.c so the simulator and the
 * firmware agree on geometry.
 */
#define SCREEN_WIDTH     800
#define SCREEN_HEIGHT    480

/*
 * Navigation bar dimensions.
 * At 0.1081 mm/pixel on this panel, 80 px is 8.6 mm: the minimum comfortable
 * finger target. The previous 40 px bar was only 4.3 mm.
 */
#define NAV_BAR_HEIGHT   80
#define NAV_BAR_Y_START  (SCREEN_HEIGHT - NAV_BAR_HEIGHT)

/* Home button: centred, derived from the screen width so it cannot drift. */
#define NAV_HOME_BTN_W   160
#define NAV_HOME_BTN_X   ((SCREEN_WIDTH - NAV_HOME_BTN_W) / 2)
#define NAV_BAR_PAD      8

/*
 * Initialize the navigation bar.
 * Must be called after lv_init() and display creation.
 */
void nav_bar_init(void);

/*
 * Show the navigation bar.
 * Called when an app is running.
 */
void nav_bar_show(void);

/*
 * Hide the navigation bar.
 * Called when returning to the launcher.
 */
void nav_bar_hide(void);

/*
 * Check if a touch event is in the navigation bar zone.
 *
 * @param x  Touch X coordinate.
 * @param y  Touch Y coordinate.
 * @return   true if touch is in nav bar area.
 */
bool nav_bar_touch_hit(int x, int y);

/*
 * Handle a touch event in the nav bar zone.
 * Called by the LVGL input callback.
 *
 * @param x  Touch X coordinate.
 * @param y  Touch Y coordinate.
 */
void nav_bar_touch_handler(int x, int y);

/*
 * Get the nav bar object (for LVGL operations).
 */
void *nav_bar_get_obj(void);

#endif /* GIGASPARK_NAV_BAR_H */
