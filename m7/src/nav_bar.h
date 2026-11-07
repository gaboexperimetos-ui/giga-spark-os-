/*
 * Gigaspark OS - Navigation Bar Header
 * Android-style OS overlay with Home button
 */

#ifndef GIGASPARK_NAV_BAR_H
#define GIGASPARK_NAV_BAR_H

#include <stdbool.h>

/* Navigation bar dimensions */
#define NAV_BAR_HEIGHT   40
#define NAV_BAR_Y_START  (272 - NAV_BAR_HEIGHT)  /* Bottom of 272px screen */

/*
 * Screen dimensions.
 * Virtual canvas only: the real Giga Display Shield is 480x800 native
 * (800x480 landscape). Migration pending; see docs/HARDWARE_FIDELITY.md.
 */
#define SCREEN_WIDTH     480
#define SCREEN_HEIGHT    272

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
