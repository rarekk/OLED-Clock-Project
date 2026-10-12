/*
 * OLED Clock - main application.
 *
 * Features:
 *   - Wi-Fi (STA) with exponential-backoff reconnect
 *   - SNTP time sync, POSIX timezone
 *   - UI screens: clock, stopwatch, calendar, mascot picker
 *   - Mascot selection and brightness persisted in NVS
 *   - Calendar events (NVS) with a web UI, OLED popup and push notification
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "sdkconfig.h"
#include "ssd1306.h"
#include "clock_ui.h"
#include "event_manager.h"
#include "web_server.h"
#include "notifier.h"

static const char *TAG = "OLED_CLOCK";

/* ---------------- Compile-time configuration ---------------- */

#define WIFI_SSID          CONFIG_OLED_CLOCK_WIFI_SSID
#define WIFI_PASS          CONFIG_OLED_CLOCK_WIFI_PASSWORD
#define WIFI_MAX_RETRY     CONFIG_OLED_CLOCK_WIFI_MAX_RETRY

#define SNTP_SERVER        CONFIG_OLED_CLOCK_SNTP_SERVER
#define TIMEZONE           CONFIG_OLED_CLOCK_TIMEZONE

#define I2C_SDA_PIN        ((gpio_num_t)CONFIG_OLED_CLOCK_I2C_SDA)
#define I2C_SCL_PIN        ((gpio_num_t)CONFIG_OLED_CLOCK_I2C_SCL)
#define I2C_SPEED_HZ       CONFIG_OLED_CLOCK_I2C_SPEED

#define BTN_MODE_PIN       ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_MODE)
#define BTN_UP_PIN         ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_UP)
#define BTN_DOWN_PIN       ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_DOWN)
#define BTN_SELECT_PIN     ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_SELECT)
#define BTN_DEBOUNCE_MS    CONFIG_OLED_CLOCK_BTN_DEBOUNCE_MS

#define NVS_NAMESPACE      "clockcfg"
#define NVS_KEY_MASCOT     "mascot"
#define NVS_KEY_BRIGHTNESS "bright"

#define LOOP_PERIOD_MS     100
#define BRIGHTNESS_STEP    48
#define BRIGHTNESS_MIN     16
#define BRIGHTNESS_MAX     255
#define FRAME_RESET        100000
#define POPUP_DURATION_MS  10000   /* how long an event popup stays up */

/* ---------------- Button handling ---------------- */

typedef struct {
    gpio_num_t pin;
    bool       last_state;         /*!< last raw reading */
    int64_t    last_debounce_time; /*!< us */
} button_t;

static button_t btn_mode   = { .pin = BTN_MODE_PIN,   .last_state = true };
static button_t btn_up     = { .pin = BTN_UP_PIN,     .last_state = true };
static button_t btn_down   = { .pin = BTN_DOWN_PIN,   .last_state = true };
static button_t btn_select = { .pin = BTN_SELECT_PIN, .last_state = true };

static void init_buttons(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BTN_MODE_PIN) | (1ULL << BTN_UP_PIN) |
                        (1ULL << BTN_DOWN_PIN) | (1ULL << BTN_SELECT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

/* Returns true exactly once per (debounced) press. Buttons are active low. */
static bool button_pressed(button_t *btn)
{
    bool current = gpio_get_level(btn->pin);
    int64_t now_us = esp_timer_get_time();

    if (current != btn->last_state) {
        if ((now_us - btn->last_debounce_time) > ((int64_t)BTN_DEBOUNCE_MS * 1000)) {
            btn->last_debounce_time = now_us;
            btn->last_state = current;
            return current == 0;   /* active low */
        }
    }
    return false;
}

/* ---------------- Wi-Fi ---------------- */

static volatile bool s_wifi_connected = false;
static volatile bool s_wifi_started   = false;
static volatile int64_t s_next_reconnect_us = 0;
static volatile int  s_reconnect_attempts = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_started = true;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d), retrying with backoff",
                 disc ? disc->reason : -1);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        s_reconnect_attempts = 0;
        s_next_reconnect_us = 0;
        ESP_LOGI(TAG, "Wi-Fi connected, got IP");
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "failed to create default Wi-Fi STA netif");
        return;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (strlen(WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "No Wi-Fi SSID configured - running offline. Set it via 'idf.py menuconfig'.");
    } else {
        ESP_LOGI(TAG, "Connecting to Wi-Fi SSID '%s'...", WIFI_SSID);
    }
}

/* Reconnect with exponential backoff, called from the main loop. */
static void wifi_service_reconnect(void)
{
    if (!s_wifi_started || s_wifi_connected) {
        return;
    }
    if (WIFI_MAX_RETRY > 0 && s_reconnect_attempts >= WIFI_MAX_RETRY) {
        return;   /* gave up */
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us < s_next_reconnect_us) {
        return;
    }

    int shift = (s_reconnect_attempts > 5) ? 5 : s_reconnect_attempts; /* 1,2,4..32 s */
    s_next_reconnect_us = now_us + ((int64_t)1000000 << shift);
    s_reconnect_attempts++;

    ESP_LOGI(TAG, "Reconnecting to Wi-Fi (attempt %d)...", s_reconnect_attempts);
    esp_wifi_connect();
}

/* ---------------- SNTP ---------------- */

static void init_sntp(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(SNTP_SERVER);
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));

    setenv("TZ", TIMEZONE, 1);
    tzset();
}

/* ---------------- NVS helpers ---------------- */

static int32_t nvs_load_i32(const char *key, int32_t fallback)
{
    nvs_handle_t handle;
    int32_t value = fallback;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_i32(handle, key, &value);
        nvs_close(handle);
    }
    return value;
}

static void nvs_save_i32(const char *key, int32_t value)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_i32(handle, key, value);
        nvs_commit(handle);
        nvs_close(handle);
    }
}

/* ---------------- Main ---------------- */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_buttons();
    wifi_init_sta();
    init_sntp();

    /* Event reminders: load from NVS and start the configuration web UI. */
    ESP_ERROR_CHECK(event_manager_init());
    notifier_init();
    if (web_server_start() != ESP_OK) {
        ESP_LOGW(TAG, "event web server did not start");
    }

    /* I2C bus */
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_SCL_PIN,
        .sda_io_num = I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    ssd1306_config_t display_cfg = SSD1306_DEFAULT_CONFIG();
    display_cfg.i2c_addr = CONFIG_OLED_CLOCK_OLED_ADDR;
    display_cfg.scl_speed_hz = I2C_SPEED_HZ;

    ssd1306_t dev;
    ESP_ERROR_CHECK(ssd1306_init(bus_handle, &display_cfg, &dev));

    /* Restore persisted settings. */
    int mascot = nvs_load_i32(NVS_KEY_MASCOT, 0);
    mascot = ((mascot % CLOCK_UI_MASCOT_COUNT) + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT;

    int brightness = nvs_load_i32(NVS_KEY_BRIGHTNESS, CONFIG_OLED_CLOCK_BRIGHTNESS);
    if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
    if (brightness > BRIGHTNESS_MAX) brightness = BRIGHTNESS_MAX;
    ssd1306_set_contrast(&dev, (uint8_t)brightness);

    /* Boot splash. */
    for (int i = 0; i < 15; i++) {
        clock_ui_render_splash(&dev, mascot, i);
        vTaskDelay(pdMS_TO_TICKS(80));
    }

    ui_mode_t mode = UI_MODE_CLOCK;
    int frame = 0;
    bool sw_running = false;
    uint32_t sw_elapsed_ms = 0;
    int64_t sw_last_us = esp_timer_get_time();

    /* Event reminder popup state. */
    char popup_title[EVENT_MANAGER_TITLE_LEN] = { 0 };
    int64_t popup_until_us = 0;

    ESP_LOGI(TAG, "System running.");

    while (1) {
        /* ---- Buttons ---- */
        if (button_pressed(&btn_mode)) {
            popup_until_us = 0;   /* any MODE press dismisses an active popup */
            mode = (ui_mode_t)((mode + 1) % UI_MODE_MAX);
            ESP_LOGI(TAG, "Mode -> %d", mode);
        }

        switch (mode) {
        case UI_MODE_CLOCK:
            if (button_pressed(&btn_up)) {
                brightness += BRIGHTNESS_STEP;
                if (brightness > BRIGHTNESS_MAX) brightness = BRIGHTNESS_MAX;
                ssd1306_set_contrast(&dev, (uint8_t)brightness);
                nvs_save_i32(NVS_KEY_BRIGHTNESS, brightness);
            }
            if (button_pressed(&btn_down)) {
                brightness -= BRIGHTNESS_STEP;
                if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
                ssd1306_set_contrast(&dev, (uint8_t)brightness);
                nvs_save_i32(NVS_KEY_BRIGHTNESS, brightness);
            }
            break;

        case UI_MODE_STOPWATCH:
            if (button_pressed(&btn_select)) {
                sw_running = !sw_running;
            }
            if (button_pressed(&btn_down)) {
                sw_running = false;
                sw_elapsed_ms = 0;
            }
            break;

        case UI_MODE_MASCOT_SELECT:
            if (button_pressed(&btn_up)) {
                mascot = (mascot + 1) % CLOCK_UI_MASCOT_COUNT;
                nvs_save_i32(NVS_KEY_MASCOT, mascot);
            }
            if (button_pressed(&btn_down)) {
                mascot = (mascot - 1 + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT;
                nvs_save_i32(NVS_KEY_MASCOT, mascot);
            }
            break;

        default:
            break;
        }

        /* ---- Accurate stopwatch (measured, not assumed) ---- */
        int64_t now_us = esp_timer_get_time();
        if (sw_running) {
            sw_elapsed_ms += (uint32_t)((now_us - sw_last_us) / 1000);
        }
        sw_last_us = now_us;

        /* ---- Time ---- */
        time_t now = 0;
        struct tm timeinfo = { 0 };
        time(&now);
        localtime_r(&now, &timeinfo);
        bool time_valid = (timeinfo.tm_year + 1900) >= 2024;

        const char *status;
        if (strlen(WIFI_SSID) == 0) {
            status = "NO WIFI";
        } else if (!s_wifi_connected) {
            status = "WI-FI...";
        } else if (!time_valid) {
            status = "SYNCING";
        } else {
            status = "NTP OK";
        }

        /* ---- Due event reminders ---- */
        if (time_valid) {
            calendar_event_t due[EVENT_MANAGER_MAX_EVENTS];
            int n = event_manager_poll_due(&timeinfo, due, EVENT_MANAGER_MAX_EVENTS);
            if (n > 0) {
                int m = (n < EVENT_MANAGER_MAX_EVENTS) ? n : EVENT_MANAGER_MAX_EVENTS;
                strlcpy(popup_title, due[0].title, sizeof(popup_title));
                popup_until_us = esp_timer_get_time() + POPUP_DURATION_MS * 1000;
                mode = UI_MODE_CALENDAR;   /* show the day it belongs to */
                ESP_LOGI(TAG, "%d event(s) due now, first: '%s'", n, popup_title);
                for (int i = 0; i < m; i++) {
                    notifier_send("REMINDER", due[i].title);
                }
            }
        }

        /* ---- Render ---- */
        if (popup_until_us > esp_timer_get_time()) {
            clock_ui_render_event_popup(&dev, popup_title, frame);
        } else {
            ui_state_t state = {
                .hours = timeinfo.tm_hour,
                .minutes = timeinfo.tm_min,
                .seconds = timeinfo.tm_sec,
                .day = timeinfo.tm_mday,
                .month = timeinfo.tm_mon + 1,
                .year = timeinfo.tm_year + 1900,
                .weekday = timeinfo.tm_wday,
                .time_valid = time_valid,
                .status = status,
                .frame = frame,
                .stopwatch_ms = sw_elapsed_ms,
                .stopwatch_running = sw_running,
                .mascot_index = mascot,
                .event_days = time_valid
                    ? event_manager_day_mask(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1)
                    : 0,
            };
            clock_ui_render(&dev, mode, &state);
        }

        frame = (frame + 1) % FRAME_RESET;

        wifi_service_reconnect();
        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
