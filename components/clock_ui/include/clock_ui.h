/*
 * clock_ui.h - Presentation layer for the OLED clock.
 *
 * Renders all screens (Digital/Analog Clock, Daily Alarm, Stopwatch with Lap,
 * Interactive Calendar / Event List, and 4 Pixel-Art Mascots) from a single
 * ui_state_t snapshot.
 */
#ifndef CLOCK_UI_H
#define CLOCK_UI_H

#include <stdint.h>
#include <stdbool.h>
#include "ssd1306.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Number of selectable pixel-art mascots (NEKO, BMO, BOO, USAGI). */
#define CLOCK_UI_MASCOT_COUNT 4

/** Clock face style on the main screen (toggled with SELECT button). */
typedef enum {
    CLOCK_STYLE_DIGITAL = 0,   /*!< Big digital time + progress bar + mascot */
    CLOCK_STYLE_ANALOG  = 1,   /*!< Analog watch face + mini digital + mascot */
    CLOCK_STYLE_MAX
} clock_style_t;

/** Which field is being edited on the Alarm screen. */
typedef enum {
    ALARM_EDIT_NONE = 0,
    ALARM_EDIT_HOUR,
    ALARM_EDIT_MIN,
    ALARM_EDIT_ENABLE,
    ALARM_EDIT_MAX
} alarm_edit_t;

/** Which field is being edited on the Set Time & Date screen. */
typedef enum {
    TIME_EDIT_NONE = 0,
    TIME_EDIT_HOUR,
    TIME_EDIT_MIN,
    TIME_EDIT_SEC,
    TIME_EDIT_DAY,
    TIME_EDIT_MONTH,
    TIME_EDIT_YEAR,
    TIME_EDIT_MAX
} time_edit_t;

/** Summary of one event for the on-screen Calendar Event List view. */
typedef struct {
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    char    title[16];
} ui_event_item_t;

/** Snapshot of everything the UI needs to draw one frame. */
typedef struct {
    int           hours;
    int           minutes;
    int           seconds;
    int           day;
    int           month;
    int           year;
    int           weekday;            /*!< 0 = Sunday .. 6 = Saturday */
    bool          time_valid;         /*!< false until NTP has synced */
    const char   *status;             /*!< short link/sync text, UPPERCASE */
    const char   *ip_addr;            /*!< IPv4 string e.g. "192.168.1.45" or "" */
    int           frame;              /*!< animation frame counter */

    /* Clock style */
    clock_style_t clock_style;        /*!< Digital vs Analog face */

    /* Daily alarm state */
    uint8_t       alarm_hour;
    uint8_t       alarm_minute;
    bool          alarm_enabled;
    bool          alarm_snoozed;
    alarm_edit_t  alarm_edit;

    /* Stopwatch state */
    uint32_t      stopwatch_ms;       /*!< elapsed stopwatch time in ms */
    uint32_t      stopwatch_lap_ms;   /*!< last recorded lap split in ms */
    uint8_t       stopwatch_lap_num;  /*!< lap counter (0 = none) */
    bool          stopwatch_running;

    /* Mascot state */
    int           mascot_index;       /*!< 0 .. CLOCK_UI_MASCOT_COUNT-1 */

    /* Calendar state */
    int           cal_year;           /*!< currently viewed calendar year */
    int           cal_month;          /*!< currently viewed calendar month (1..12) */
    uint32_t      event_days;         /*!< bit (day-1) set if an event exists in cal_month */
    bool          cal_show_list;      /*!< false = month grid, true = event list */
    int           cal_event_count;    /*!< total stored events */
    ui_event_item_t cal_events[3];    /*!< up to 3 preview items for list view */

    /* Manual Date & Time editor state */
    time_edit_t   time_edit;
} ui_state_t;

typedef enum {
    UI_MODE_CLOCK = 0,
    UI_MODE_ALARM,
    UI_MODE_STOPWATCH,
    UI_MODE_CALENDAR,
    UI_MODE_MASCOT_SELECT,
    UI_MODE_SET_TIME,
    UI_MODE_MAX
} ui_mode_t;

/** Render one frame of the given screen. */
void clock_ui_render(ssd1306_t *dev, ui_mode_t mode, const ui_state_t *state);

/** Draw a boot splash using the given mascot. */
void clock_ui_render_splash(ssd1306_t *dev, int mascot_index, int frame);

/**
 * Draw a full-screen popup for a ringing alarm or due event reminder.
 * Shows animated mascot, title, and button hints ([OK]STOP [UP]SNZ 5M).
 */
void clock_ui_render_event_popup(ssd1306_t *dev, const char *header,
                                 const char *title, int mascot_index, int frame);

/** Human readable name for a mascot index (never NULL). */
const char *clock_ui_mascot_name(int index);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_UI_H */
