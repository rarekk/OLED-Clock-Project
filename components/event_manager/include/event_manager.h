/*
 * event_manager.h - Persisted calendar events (RTC reminders).
 *
 * Up to EVENT_MANAGER_MAX_EVENTS events are kept in RAM and mirrored to NVS
 * flash so they survive reboots. The storage format is a fixed-size blob, so
 * this is intended for the same firmware reading back what it wrote.
 */
#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_MANAGER_MAX_EVENTS 10
#define EVENT_MANAGER_TITLE_LEN  32

typedef struct {
    uint16_t year;
    uint8_t  month;   /*!< 1-12 */
    uint8_t  day;     /*!< 1-31 */
    uint8_t  hour;    /*!< 0-23 */
    uint8_t  minute;  /*!< 0-59 */
    char     title[EVENT_MANAGER_TITLE_LEN];
    bool     active;
} calendar_event_t;

typedef struct {
    uint8_t  hour;    /*!< 0-23 */
    uint8_t  minute;  /*!< 0-59 */
    bool     enabled;
} alarm_config_t;

/** Load events and daily alarm from NVS into RAM. Call once after nvs_flash_init(). */
esp_err_t event_manager_init(void);

/** Read the current daily alarm configuration. */
void event_manager_get_alarm(alarm_config_t *out);

/** Update and persist the daily alarm configuration to NVS. */
esp_err_t event_manager_set_alarm(const alarm_config_t *cfg);

/**
 * Check if the daily alarm (or active snooze) is due at `now`.
 * Returns true once for the matching minute.
 */
bool event_manager_poll_alarm(const struct tm *now);

/** Snooze the daily alarm for `minutes` (1..60) from `now`. */
void event_manager_snooze_alarm(const struct tm *now, int minutes);

/** Cancel any pending snooze timer. */
void event_manager_clear_snooze(void);

/** True if a snooze countdown is currently active. */
bool event_manager_is_snoozed(void);

/** Number of stored (active) events. */
int event_manager_count(void);

/** Read-only access to event `index`, or NULL if out of range. */
const calendar_event_t *event_manager_get(int index);

/** Add (or update nothing) an event. Validates the date/time and stores it. */
esp_err_t event_manager_add(const calendar_event_t *ev);

/** Add an event only if an identical date+time+title entry does not already exist. */
esp_err_t event_manager_upsert(const calendar_event_t *ev);

/** Remove event `index` (compacts the list). */
esp_err_t event_manager_remove(int index);

/** Remove every event. */
esp_err_t event_manager_clear(void);

/** Count active events on a specific calendar date. */
int event_manager_events_on_day(int year, int month, int day);

/** Bitmap of days (bit `day-1`) in the given month that have >=1 event. */
uint32_t event_manager_day_mask(int year, int month);

/**
 * Return events whose exact date+time matches `now` and that have not been
 * returned for this minute yet. Copies up to `max_out` into `out` and returns
 * the number of matches (which may exceed max_out).
 */
int event_manager_poll_due(const struct tm *now, calendar_event_t *out, int max_out);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_MANAGER_H */
