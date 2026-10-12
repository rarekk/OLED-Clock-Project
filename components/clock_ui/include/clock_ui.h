/*
 * clock_ui.h - Presentation layer for the OLED clock.
 *
 * The UI has three screens (clock, stopwatch, mascot picker) that are
 * rendered from a single ui_state_t snapshot, so callers never have to
 * remember the order of a dozen positional arguments.
 */
#ifndef CLOCK_UI_H
#define CLOCK_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of selectable mascots. */
#define CLOCK_UI_MASCOT_COUNT 3

/** Snapshot of everything the UI needs to draw one frame. */
typedef struct {
    int      hours;
    int      minutes;
    int      seconds;
    int      day;
    int      month;
    int      year;
    int      weekday;            /*!< 0 = Sunday .. 6 = Saturday */
    bool     time_valid;         /*!< false until NTP has synced */
    const char *status;          /*!< short link/sync text, UPPERCASE, <= 10 chars */
    int      frame;              /*!< animation frame counter */
    uint32_t stopwatch_ms;       /*!< elapsed stopwatch time in milliseconds */
    bool     stopwatch_running;
    int      mascot_index;       /*!< 0 .. CLOCK_UI_MASCOT_COUNT-1 */
    uint32_t event_days;         /*!< bit (day-1) set if an event exists that day */
} ui_state_t;

typedef enum {
    UI_MODE_CLOCK = 0,
    UI_MODE_STOPWATCH,
    UI_MODE_CALENDAR,
    UI_MODE_MASCOT_SELECT,
    UI_MODE_MAX
} ui_mode_t;

/** Render one frame of the given screen. */
void clock_ui_render(ssd1306_t *dev, ui_mode_t mode, const ui_state_t *state);

/** Draw a boot splash using the given mascot. */
void clock_ui_render_splash(ssd1306_t *dev, int mascot_index, int frame);

/** Draw a full-screen "reminder" popup for a due event (title is uppercased). */
void clock_ui_render_event_popup(ssd1306_t *dev, const char *title, int frame);

/** Human readable name for a mascot index (never NULL). */
const char *clock_ui_mascot_name(int index);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_UI_H */
