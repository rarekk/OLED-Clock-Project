#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "event_manager.h"
#include "gcal_sync.h"

static const char *TAG = "gcal";

#define NVS_NAMESPACE       "gcal"
#define NVS_KEY_URL         "url"
#define RX_BUF_SIZE         1536
#define SYNC_INTERVAL_US    (15LL * 60LL * 1000000LL)   /* 15 minutes */

#ifndef CONFIG_OLED_CLOCK_GCAL_URL
#define CONFIG_OLED_CLOCK_GCAL_URL "https://script.google.com/macros/s/AKfycbyeW6dGVLh1M0cM-BAhQZGBNPZD8giip6qWJs_HUUzoT9wB0W0ouJUt2bdIUA-Klana/exec"
#endif

static char          s_url[GCAL_URL_MAX_LEN] = { 0 };
static char          s_status[32]            = "NO URL";
static volatile bool s_sync_in_progress      = false;
static volatile bool s_sync_requested        = false;
static int64_t       s_next_sync_us          = 0;

typedef struct {
    char   buf[RX_BUF_SIZE];
    size_t len;
} rx_accum_t;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    rx_accum_t *acc = (rx_accum_t *)evt->user_data;
    if (acc == NULL) {
        return ESP_OK;
    }

    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        /* When Google Apps Script responds with a 302 Location redirect,
         * discard the intermediate HTML redirect body so only the final
         * payload from script.googleusercontent.com is kept. */
        if (evt->header_key && strcasecmp(evt->header_key, "Location") == 0) {
            acc->len = 0;
            acc->buf[0] = '\0';
        }
        break;

    case HTTP_EVENT_ON_DATA:
        if (evt->data && evt->data_len > 0) {
            size_t avail = (sizeof(acc->buf) - 1) - acc->len;
            size_t copy = ((size_t)evt->data_len < avail) ? (size_t)evt->data_len : avail;
            if (copy > 0) {
                memcpy(acc->buf + acc->len, evt->data, copy);
                acc->len += copy;
                acc->buf[acc->len] = '\0';
            }
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}

/* Parse lines of format: YYYY-MM-DD HH:MM|TITLE */
static int parse_and_store_events(char *payload)
{
    int added = 0;
    char *saveptr = NULL;
    char *line = strtok_r(payload, "\r\n", &saveptr);

    while (line != NULL) {
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        char *sep = strchr(line, '|');
        if (sep != NULL) {
            *sep = '\0';
            const char *title = sep + 1;
            unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0;
            if (sscanf(line, "%u-%u-%u %u:%u", &y, &mo, &d, &h, &mi) == 5 &&
                title[0] != '\0') {
                calendar_event_t ev = {
                    .year   = (uint16_t)y,
                    .month  = (uint8_t)mo,
                    .day    = (uint8_t)d,
                    .hour   = (uint8_t)h,
                    .minute = (uint8_t)mi,
                    .active = true,
                };
                strlcpy(ev.title, title, sizeof(ev.title));
                if (event_manager_upsert(&ev) == ESP_OK) {
                    added++;
                }
            }
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
    return added;
}

static void sync_task(void *arg)
{
    (void)arg;

    rx_accum_t *acc = calloc(1, sizeof(rx_accum_t));
    if (acc == NULL) {
        strlcpy(s_status, "ERR MEM", sizeof(s_status));
        s_sync_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    strlcpy(s_status, "SYNCING...", sizeof(s_status));
    ESP_LOGI(TAG, "Fetching Google Calendar events from %s", s_url);

    esp_http_client_config_t cfg = {
        .url                   = s_url,
        .method                = HTTP_METHOD_GET,
        .timeout_ms            = 15000,
        .crt_bundle_attach     = esp_crt_bundle_attach,
        .event_handler         = http_event_cb,
        .user_data             = acc,
        .max_redirection_count = 5,
        .buffer_size           = 2048,   /* Large enough for Google 302 Location headers */
        .buffer_size_tx        = 1024,
        .keep_alive_enable     = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        strlcpy(s_status, "ERR INIT", sizeof(s_status));
        free(acc);
        s_sync_in_progress = false;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        int count = parse_and_store_events(acc->buf);
        snprintf(s_status, sizeof(s_status), "OK (%d EV)", count);
        ESP_LOGI(TAG, "Google Calendar sync complete: %d event(s) synced", count);
    } else {
        snprintf(s_status, sizeof(s_status), "ERR %d", status);
        ESP_LOGW(TAG, "Google Calendar sync failed: err=%s http=%d",
                 esp_err_to_name(err), status);
    }

    free(acc);
    s_sync_in_progress = false;
    vTaskDelete(NULL);
}

esp_err_t gcal_sync_init(void)
{
    strlcpy(s_url, CONFIG_OLED_CLOCK_GCAL_URL, sizeof(s_url));

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        char nvs_url[GCAL_URL_MAX_LEN] = { 0 };
        size_t len = sizeof(nvs_url);
        if (nvs_get_str(h, NVS_KEY_URL, nvs_url, &len) == ESP_OK && nvs_url[0] != '\0') {
            strlcpy(s_url, nvs_url, sizeof(s_url));
        }
        nvs_close(h);
    }

    if (s_url[0] != '\0') {
        strlcpy(s_status, "READY", sizeof(s_status));
        s_sync_requested = true;
        ESP_LOGI(TAG, "Google Calendar URL configured");
    } else {
        strlcpy(s_status, "NO URL", sizeof(s_status));
    }
    return ESP_OK;
}

void gcal_sync_get_url(char *out, size_t outlen)
{
    if (out != NULL && outlen > 0) {
        strlcpy(out, s_url, outlen);
    }
}

esp_err_t gcal_sync_set_url(const char *url)
{
    if (url == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    strlcpy(s_url, url, sizeof(s_url));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, NVS_KEY_URL, s_url);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }

    if (s_url[0] != '\0') {
        strlcpy(s_status, "QUEUED", sizeof(s_status));
        s_sync_requested = true;
    } else {
        strlcpy(s_status, "NO URL", sizeof(s_status));
    }
    return err;
}

void gcal_sync_trigger_now(void)
{
    if (s_url[0] != '\0') {
        s_sync_requested = true;
    }
}

void gcal_sync_service(bool wifi_connected)
{
    if (!wifi_connected || s_url[0] == '\0' || s_sync_in_progress) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (!s_sync_requested && (s_next_sync_us == 0 || now_us < s_next_sync_us)) {
        if (s_next_sync_us == 0) {
            s_next_sync_us = now_us + 5000000LL;   /* First sync 5s after Wi-Fi up */
        }
        return;
    }

    s_sync_requested = false;
    s_next_sync_us = now_us + SYNC_INTERVAL_US;
    s_sync_in_progress = true;

    if (xTaskCreate(sync_task, "gcal_sync", 8192, NULL, 4, NULL) != pdPASS) {
        s_sync_in_progress = false;
        strlcpy(s_status, "ERR TASK", sizeof(s_status));
    }
}

const char *gcal_sync_status(void)
{
    return s_status;
}
