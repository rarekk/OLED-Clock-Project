#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "event_manager.h"

static const char *TAG = "events";

#define NVS_NAMESPACE "events"
#define NVS_KEY_BLOB  "items"
#define NVS_KEY_COUNT "count"

static calendar_event_t s_events[EVENT_MANAGER_MAX_EVENTS];
static bool             s_fired[EVENT_MANAGER_MAX_EVENTS];   /*!< RAM-only: fired this minute */
static int              s_count = 0;
static int64_t          s_last_minute_key = -1;

/* ---- helpers ---- */

static bool valid_event(const calendar_event_t *e)
{
    if (e == NULL) return false;
    if (e->month < 1 || e->month > 12) return false;
    if (e->day < 1 || e->day > 31) return false;
    if (e->hour > 23 || e->minute > 59) return false;
    if (e->title[0] == '\0') return false;
    return true;
}

static esp_err_t save_all(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(RW) failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(h, NVS_KEY_BLOB, s_events, sizeof(s_events));
    if (err == ESP_OK) {
        err = nvs_set_i32(h, NVS_KEY_COUNT, s_count);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* ---- public API ---- */

esp_err_t event_manager_init(void)
{
    memset(s_events, 0, sizeof(s_events));
    memset(s_fired, 0, sizeof(s_fired));
    s_count = 0;
    s_last_minute_key = -1;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no events stored yet");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(RO) failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t blob_size = sizeof(s_events);
    err = nvs_get_blob(h, NVS_KEY_BLOB, s_events, &blob_size);
    if (err == ESP_OK) {
        int32_t count = 0;
        nvs_get_i32(h, NVS_KEY_COUNT, &count);
        if (count < 0 || count > EVENT_MANAGER_MAX_EVENTS) {
            count = 0;
        }
        s_count = (int)count;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }

    nvs_close(h);
    ESP_LOGI(TAG, "loaded %d event(s)", s_count);
    return err;
}

int event_manager_count(void)
{
    return s_count;
}

const calendar_event_t *event_manager_get(int index)
{
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return &s_events[index];
}

esp_err_t event_manager_add(const calendar_event_t *ev)
{
    if (!valid_event(ev)) {
        ESP_LOGW(TAG, "rejected invalid event");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_count >= EVENT_MANAGER_MAX_EVENTS) {
        ESP_LOGW(TAG, "event list full (%d)", EVENT_MANAGER_MAX_EVENTS);
        return ESP_ERR_NO_MEM;
    }

    s_events[s_count] = *ev;
    s_events[s_count].active = true;
    s_count++;

    ESP_LOGI(TAG, "added '%s' on %04d-%02d-%02d %02d:%02d",
             ev->title, ev->year, ev->month, ev->day, ev->hour, ev->minute);
    return save_all();
}

esp_err_t event_manager_remove(int index)
{
    if (index < 0 || index >= s_count) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = index; i < s_count - 1; i++) {
        s_events[i] = s_events[i + 1];
    }
    s_count--;
    memset(&s_events[s_count], 0, sizeof(s_events[0]));
    ESP_LOGI(TAG, "removed event %d, %d left", index, s_count);
    return save_all();
}

esp_err_t event_manager_clear(void)
{
    memset(s_events, 0, sizeof(s_events));
    s_count = 0;
    ESP_LOGI(TAG, "cleared all events");
    return save_all();
}

int event_manager_events_on_day(int year, int month, int day)
{
    int n = 0;
    for (int i = 0; i < s_count; i++) {
        const calendar_event_t *e = &s_events[i];
        if (e->active && e->year == year && e->month == month && e->day == day) {
            n++;
        }
    }
    return n;
}

uint32_t event_manager_day_mask(int year, int month)
{
    uint32_t mask = 0;
    for (int d = 1; d <= 31; d++) {
        if (event_manager_events_on_day(year, month, d) > 0) {
            mask |= (1u << (d - 1));
        }
    }
    return mask;
}

int event_manager_poll_due(const struct tm *now, calendar_event_t *out, int max_out)
{
    if (now == NULL) {
        return 0;
    }

    int64_t key = (int64_t)(now->tm_year + 1900) * 100000000LL
                + (int64_t)(now->tm_mon + 1) * 1000000LL
                + (int64_t)now->tm_mday * 10000LL
                + (int64_t)now->tm_hour * 100LL
                + now->tm_min;

    if (key != s_last_minute_key) {
        s_last_minute_key = key;
        memset(s_fired, 0, sizeof(s_fired));
    }

    int year = now->tm_year + 1900;
    int month = now->tm_mon + 1;
    int n = 0;

    for (int i = 0; i < s_count; i++) {
        calendar_event_t *e = &s_events[i];
        if (!e->active) continue;
        if (e->year != year || e->month != month || e->day != now->tm_mday) continue;
        if (e->hour != now->tm_hour || e->minute != now->tm_min) continue;
        if (s_fired[i]) continue;

        s_fired[i] = true;
        if (out != NULL && n < max_out) {
            out[n] = *e;
        }
        n++;
    }
    return n;
}
