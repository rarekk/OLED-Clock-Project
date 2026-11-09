/*
 * OLED Clock - main application.
 *
 * Features:
 *   - Wi-Fi (STA) with exponential-backoff reconnect
 *   - SNTP time sync, POSIX timezone
 *   - UI screens: Clock (Digital / Analog), Daily Alarm, Stopwatch (with Lap),
 *                 Interactive Calendar / Event List, 4 Pixel-Art Mascots
 *   - Buzzer module (LEDC PWM) for alarm melodies, test preview & key clicks
 *   - Mascot selection, clock face style, alarm and brightness persisted in NVS
 *   - Calendar events & daily alarm configurable via 4 buttons AND phone Web UI
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

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
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "sdkconfig.h"
#include "ssd1306.h"
#include "clock_ui.h"
#include "event_manager.h"
#include "gcal_sync.h"
#include "web_server.h"
#include "notifier.h"
#include "buzzer.h"

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

/* 4-Button Module (K1=Green/MODE, K2=Blue/UP, K3=Yellow/DOWN, K4=Red/SELECT) */
#define BTN_MODE_PIN       ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_MODE)
#define BTN_UP_PIN         ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_UP)
#define BTN_DOWN_PIN       ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_DOWN)
#define BTN_SELECT_PIN     ((gpio_num_t)CONFIG_OLED_CLOCK_BTN_SELECT)
#define BTN_DEBOUNCE_MS    CONFIG_OLED_CLOCK_BTN_DEBOUNCE_MS

#ifndef CONFIG_OLED_CLOCK_BUZZER_PIN
#define CONFIG_OLED_CLOCK_BUZZER_PIN 25
#endif
#ifndef CONFIG_OLED_CLOCK_BUZZER_ACTIVE_LOW
#define CONFIG_OLED_CLOCK_BUZZER_ACTIVE_LOW 0
#endif

#define BUZZER_PIN         ((gpio_num_t)CONFIG_OLED_CLOCK_BUZZER_PIN)
#define BUZZER_ACTIVE_LOW  ((bool)CONFIG_OLED_CLOCK_BUZZER_ACTIVE_LOW)

#define NVS_NAMESPACE      "clockcfg"
#define NVS_KEY_MASCOT     "mascot"
#define NVS_KEY_BRIGHTNESS "bright"
#define NVS_KEY_STYLE      "style"

#define LOOP_PERIOD_MS     100
#define BRIGHTNESS_STEP    48
#define BRIGHTNESS_MIN     16
#define BRIGHTNESS_MAX     255
#define FRAME_RESET        100000
#define POPUP_DURATION_MS  30000   /* how long an alarm/reminder rings before auto-timeout */

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
            if (current == 0) {   /* active low press */
                buzzer_click();
                return true;
            }
        }
    }
    return false;
}

/* ---------------- Wi-Fi ---------------- */

static volatile bool s_wifi_connected = false;
static volatile bool s_wifi_started   = false;
static volatile int64_t s_next_reconnect_us = 0;
static volatile int  s_reconnect_attempts = 0;
static char          s_ip_addr[20] = { 0 };

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_wifi_started = true;
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        s_ip_addr[0] = '\0';
        wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d), retrying with backoff",
                 disc ? disc->reason : -1);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        if (event != NULL) {
            snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        }
        s_wifi_connected = true;
        s_reconnect_attempts = 0;
        s_next_reconnect_us = 0;
        ESP_LOGI(TAG, "================================================");
        ESP_LOGI(TAG, "Wi-Fi connected! Open Dashboard: http://%s/", s_ip_addr);
        ESP_LOGI(TAG, "================================================");
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

static void apply_manual_time(const struct tm *tm_val)
{
    struct tm copy = *tm_val;
    copy.tm_isdst = -1;
    time_t epoch = mktime(&copy);
    if (epoch != (time_t)-1) {
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
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
    buzzer_init(BUZZER_PIN, BUZZER_ACTIVE_LOW);
    wifi_init_sta();
    init_sntp();

    /* Event reminders & daily alarm: load from NVS and start the web UI. */
    ESP_ERROR_CHECK(event_manager_init());
    gcal_sync_init();
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
    bool oled_ok = (ssd1306_init(bus_handle, &display_cfg, &dev) == ESP_OK);
    if (!oled_ok) {
        ESP_LOGW(TAG, "OLED not responding on I2C (SDA=%d, SCL=%d, addr=0x%02X). "
                      "Check OLED wiring! Continuing so Web Dashboard & Buzzer stay online.",
                 (int)I2C_SDA_PIN, (int)I2C_SCL_PIN, display_cfg.i2c_addr);
    }

    /* Restore persisted settings. */
    int mascot = nvs_load_i32(NVS_KEY_MASCOT, 0);
    mascot = ((mascot % CLOCK_UI_MASCOT_COUNT) + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT;

    int style = nvs_load_i32(NVS_KEY_STYLE, CLOCK_STYLE_DIGITAL);
    clock_style_t clock_style = (style == CLOCK_STYLE_ANALOG)
                                ? CLOCK_STYLE_ANALOG : CLOCK_STYLE_DIGITAL;

    int brightness = nvs_load_i32(NVS_KEY_BRIGHTNESS, CONFIG_OLED_CLOCK_BRIGHTNESS);
    if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
    if (brightness > BRIGHTNESS_MAX) brightness = BRIGHTNESS_MAX;
    ssd1306_set_contrast(&dev, (uint8_t)brightness);

    /* Boot splash + startup chime. */
    buzzer_confirm();
    for (int i = 0; i < 15; i++) {
        clock_ui_render_splash(&dev, mascot, i);
        vTaskDelay(pdMS_TO_TICKS(70));
    }

    ui_mode_t mode = UI_MODE_CLOCK;
    int frame = 0;

    /* Stopwatch state */
    bool     sw_running    = false;
    uint32_t sw_elapsed_ms = 0;
    uint32_t sw_lap_ms     = 0;
    uint8_t  sw_lap_num    = 0;
    int64_t  sw_last_us    = esp_timer_get_time();

    /* Alarm & Time editor state */
    alarm_edit_t alarm_edit = ALARM_EDIT_NONE;
    time_edit_t  time_edit  = TIME_EDIT_NONE;

    /* Interactive calendar view state (0 = track current month) */
    int  cal_view_year  = 0;
    int  cal_view_month = 0;
    bool cal_show_list  = false;

    /* Active popup state (alarm or event reminder) */
    char    popup_header[20] = { 0 };
    char    popup_title[EVENT_MANAGER_TITLE_LEN] = { 0 };
    int64_t popup_until_us   = 0;

    ESP_LOGI(TAG, "System running.");

    while (1) {
        /* ---- Current Time ---- */
        time_t now = 0;
        struct tm timeinfo = { 0 };
        time(&now);
        localtime_r(&now, &timeinfo);
        bool time_valid = (timeinfo.tm_year + 1900) >= 2024;

        if (cal_view_year == 0) {
            cal_view_year  = time_valid ? (timeinfo.tm_year + 1900) : 2026;
            cal_view_month = time_valid ? (timeinfo.tm_mon + 1)     : 1;
        }

        int64_t now_us = esp_timer_get_time();
        bool popup_active = (popup_until_us > now_us);

        /* Auto-silence buzzer when popup expires */
        if (!popup_active && buzzer_alarm_is_playing()) {
            buzzer_alarm_stop();
        }

        /* ---- Buttons (4-Key Module: MODE, UP, DOWN, SELECT) ---- */
        bool p_mode   = button_pressed(&btn_mode);
        bool p_up     = button_pressed(&btn_up);
        bool p_down   = button_pressed(&btn_down);
        bool p_select = button_pressed(&btn_select);

        if (popup_active) {
            /* While Alarm / Reminder popup is ringing:
             *   - MODE (Green) or SELECT (Red) -> Stop / Dismiss
             *   - UP (Blue) or DOWN (Yellow)   -> Snooze 5 minutes
             */
            if (p_mode || p_select) {
                popup_until_us = 0;
                buzzer_alarm_stop();
                event_manager_clear_snooze();
                buzzer_confirm();
                ESP_LOGI(TAG, "Alarm/reminder dismissed");
            } else if (p_up || p_down) {
                popup_until_us = 0;
                buzzer_alarm_stop();
                if (time_valid) {
                    event_manager_snooze_alarm(&timeinfo, 5);
                }
                buzzer_confirm();
                ESP_LOGI(TAG, "Alarm/reminder snoozed for 5m");
            }
        } else {
            /* Normal navigation across screens */
            if (p_mode) {
                alarm_edit = ALARM_EDIT_NONE;
                time_edit  = TIME_EDIT_NONE;
                cal_show_list = false;
                mode = (ui_mode_t)((mode + 1) % UI_MODE_MAX);
                if (mode == UI_MODE_CALENDAR && time_valid) {
                    cal_view_year  = timeinfo.tm_year + 1900;
                    cal_view_month = timeinfo.tm_mon + 1;
                }
                ESP_LOGI(TAG, "Mode -> %d", mode);
            }

            switch (mode) {
            case UI_MODE_CLOCK:
                if (p_select) {
                    /* Toggle between Digital Clock and Analog Watch Face */
                    clock_style = (clock_style == CLOCK_STYLE_DIGITAL)
                                  ? CLOCK_STYLE_ANALOG : CLOCK_STYLE_DIGITAL;
                    nvs_save_i32(NVS_KEY_STYLE, (int32_t)clock_style);
                    buzzer_confirm();
                }
                if (p_up) {
                    brightness += BRIGHTNESS_STEP;
                    if (brightness > BRIGHTNESS_MAX) brightness = BRIGHTNESS_MAX;
                    ssd1306_set_contrast(&dev, (uint8_t)brightness);
                    nvs_save_i32(NVS_KEY_BRIGHTNESS, brightness);
                }
                if (p_down) {
                    brightness -= BRIGHTNESS_STEP;
                    if (brightness < BRIGHTNESS_MIN) brightness = BRIGHTNESS_MIN;
                    ssd1306_set_contrast(&dev, (uint8_t)brightness);
                    nvs_save_i32(NVS_KEY_BRIGHTNESS, brightness);
                }
                break;

            case UI_MODE_ALARM: {
                alarm_config_t al = { 0 };
                event_manager_get_alarm(&al);

                if (p_select) {
                    alarm_edit = (alarm_edit_t)((alarm_edit + 1) % ALARM_EDIT_MAX);
                    if (alarm_edit == ALARM_EDIT_NONE) {
                        buzzer_confirm();
                    }
                }
                if (alarm_edit == ALARM_EDIT_NONE) {
                    /* Quick actions in view mode: UP = toggle ON/OFF, DOWN = test buzzer */
                    if (p_up) {
                        al.enabled = !al.enabled;
                        event_manager_set_alarm(&al);
                        buzzer_confirm();
                    }
                    if (p_down) {
                        if (buzzer_alarm_is_playing()) {
                            buzzer_alarm_stop();
                        } else {
                            buzzer_confirm();
                        }
                    }
                } else if (alarm_edit == ALARM_EDIT_HOUR) {
                    if (p_up) {
                        al.hour = (al.hour + 1) % 24;
                        event_manager_set_alarm(&al);
                    }
                    if (p_down) {
                        al.hour = (al.hour + 23) % 24;
                        event_manager_set_alarm(&al);
                    }
                } else if (alarm_edit == ALARM_EDIT_MIN) {
                    if (p_up) {
                        al.minute = (al.minute + 1) % 60;
                        event_manager_set_alarm(&al);
                    }
                    if (p_down) {
                        al.minute = (al.minute + 59) % 60;
                        event_manager_set_alarm(&al);
                    }
                } else if (alarm_edit == ALARM_EDIT_ENABLE) {
                    if (p_up || p_down) {
                        al.enabled = !al.enabled;
                        event_manager_set_alarm(&al);
                    }
                }
                break;
            }

            case UI_MODE_STOPWATCH:
                if (p_select) {
                    sw_running = !sw_running;
                    buzzer_confirm();
                }
                if (p_up && sw_running) {
                    sw_lap_ms = sw_elapsed_ms;
                    sw_lap_num++;
                }
                if (p_down) {
                    sw_running    = false;
                    sw_elapsed_ms = 0;
                    sw_lap_ms     = 0;
                    sw_lap_num    = 0;
                }
                break;

            case UI_MODE_CALENDAR:
                if (p_select) {
                    cal_show_list = !cal_show_list;
                }
                if (cal_show_list) {
                    if (p_down) {
                        gcal_sync_trigger_now();
                        buzzer_confirm();
                    }
                } else {
                    if (p_up) {
                        cal_view_month++;
                        if (cal_view_month > 12) {
                            cal_view_month = 1;
                            cal_view_year++;
                        }
                    }
                    if (p_down) {
                        cal_view_month--;
                        if (cal_view_month < 1) {
                            cal_view_month = 12;
                            cal_view_year--;
                        }
                    }
                }
                break;

            case UI_MODE_MASCOT_SELECT:
                if (p_up || p_select) {
                    mascot = (mascot + 1) % CLOCK_UI_MASCOT_COUNT;
                    nvs_save_i32(NVS_KEY_MASCOT, mascot);
                }
                if (p_down) {
                    mascot = (mascot - 1 + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT;
                    nvs_save_i32(NVS_KEY_MASCOT, mascot);
                }
                break;

            case UI_MODE_SET_TIME: {
                if (p_select) {
                    if (!time_valid) {
                        timeinfo.tm_year = 2026 - 1900;
                        timeinfo.tm_mon  = 0;
                        timeinfo.tm_mday = 1;
                        timeinfo.tm_hour = 12;
                        timeinfo.tm_min  = 0;
                        timeinfo.tm_sec  = 0;
                        apply_manual_time(&timeinfo);
                        time(&now);
                        localtime_r(&now, &timeinfo);
                        time_valid = true;
                    }
                    time_edit = (time_edit_t)((time_edit + 1) % TIME_EDIT_MAX);
                    if (time_edit == TIME_EDIT_NONE) {
                        buzzer_confirm();
                    }
                }
                if (time_edit == TIME_EDIT_NONE && p_down) {
                    esp_sntp_restart();
                    buzzer_confirm();
                    ESP_LOGI(TAG, "SNTP resync requested");
                }
                if (time_edit != TIME_EDIT_NONE && (p_up || p_down)) {
                    int delta = p_up ? 1 : -1;
                    if (!time_valid) {
                        timeinfo.tm_year = 2026 - 1900;
                        timeinfo.tm_mon  = 0;
                        timeinfo.tm_mday = 1;
                    }
                    switch (time_edit) {
                    case TIME_EDIT_HOUR:
                        timeinfo.tm_hour = (timeinfo.tm_hour + delta + 24) % 24;
                        break;
                    case TIME_EDIT_MIN:
                        timeinfo.tm_min = (timeinfo.tm_min + delta + 60) % 60;
                        break;
                    case TIME_EDIT_SEC:
                        timeinfo.tm_sec = (timeinfo.tm_sec + delta + 60) % 60;
                        break;
                    case TIME_EDIT_DAY:
                        timeinfo.tm_mday += delta;
                        if (timeinfo.tm_mday < 1)  timeinfo.tm_mday = 31;
                        if (timeinfo.tm_mday > 31) timeinfo.tm_mday = 1;
                        break;
                    case TIME_EDIT_MONTH:
                        timeinfo.tm_mon = (timeinfo.tm_mon + delta + 12) % 12;
                        break;
                    case TIME_EDIT_YEAR: {
                        int y = (timeinfo.tm_year + 1900) + delta;
                        if (y < 2024) y = 2099;
                        if (y > 2099) y = 2024;
                        timeinfo.tm_year = y - 1900;
                        break;
                    }
                    default:
                        break;
                    }
                    apply_manual_time(&timeinfo);
                    time(&now);
                    localtime_r(&now, &timeinfo);
                    time_valid = (timeinfo.tm_year + 1900) >= 2024;
                }
                break;
            }

            default:
                break;
            }
        }

        /* ---- Accurate stopwatch (measured, not assumed) ---- */
        now_us = esp_timer_get_time();
        if (sw_running) {
            sw_elapsed_ms += (uint32_t)((now_us - sw_last_us) / 1000);
        }
        sw_last_us = now_us;

        const char *status;
        if (time_valid && s_wifi_connected) {
            status = "NTP OK";
        } else if (time_valid) {
            status = "TIME OK";
        } else if (strlen(WIFI_SSID) == 0) {
            status = "NO WIFI";
        } else if (!s_wifi_connected) {
            status = "WI-FI...";
        } else {
            status = "SYNCING";
        }

        /* ---- Check Daily Alarm & Due Event Reminders ---- */
        if (time_valid) {
            if (event_manager_poll_alarm(&timeinfo)) {
                strlcpy(popup_header, "! ALARM !", sizeof(popup_header));
                snprintf(popup_title, sizeof(popup_title), "%02u:%02u WAKE UP",
                         (unsigned)(timeinfo.tm_hour % 24),
                         (unsigned)(timeinfo.tm_min % 60));
                popup_until_us = esp_timer_get_time() + POPUP_DURATION_MS * 1000;
                buzzer_alarm_start();
                notifier_send("ALARM", popup_title);
                ESP_LOGI(TAG, "Daily alarm triggered at %02d:%02d",
                         timeinfo.tm_hour, timeinfo.tm_min);
            }

            calendar_event_t due[EVENT_MANAGER_MAX_EVENTS];
            int n = event_manager_poll_due(&timeinfo, due, EVENT_MANAGER_MAX_EVENTS);
            if (n > 0) {
                int m = (n < EVENT_MANAGER_MAX_EVENTS) ? n : EVENT_MANAGER_MAX_EVENTS;
                strlcpy(popup_header, "! REMINDER !", sizeof(popup_header));
                strlcpy(popup_title, due[0].title, sizeof(popup_title));
                popup_until_us = esp_timer_get_time() + POPUP_DURATION_MS * 1000;
                mode = UI_MODE_CALENDAR;
                buzzer_alarm_start();
                ESP_LOGI(TAG, "%d event(s) due now, first: '%s'", n, popup_title);
                for (int i = 0; i < m; i++) {
                    notifier_send("REMINDER", due[i].title);
                }
            }
        }

        /* ---- Render ---- */
        if (popup_until_us > esp_timer_get_time()) {
            clock_ui_render_event_popup(&dev, popup_header, popup_title, mascot, frame);
        } else {
            alarm_config_t al = { 0 };
            event_manager_get_alarm(&al);

            ui_state_t state = {
                .hours             = timeinfo.tm_hour,
                .minutes           = timeinfo.tm_min,
                .seconds           = timeinfo.tm_sec,
                .day               = timeinfo.tm_mday,
                .month             = timeinfo.tm_mon + 1,
                .year              = timeinfo.tm_year + 1900,
                .weekday           = timeinfo.tm_wday,
                .time_valid        = time_valid,
                .status            = status,
                .ip_addr           = s_ip_addr,
                .frame             = frame,
                .clock_style       = clock_style,
                .alarm_hour        = al.hour,
                .alarm_minute      = al.minute,
                .alarm_enabled     = al.enabled,
                .alarm_snoozed     = event_manager_is_snoozed(),
                .alarm_edit        = alarm_edit,
                .stopwatch_ms      = sw_elapsed_ms,
                .stopwatch_lap_ms  = sw_lap_ms,
                .stopwatch_lap_num = sw_lap_num,
                .stopwatch_running = sw_running,
                .mascot_index      = mascot,
                .cal_year          = cal_view_year,
                .cal_month         = cal_view_month,
                .event_days        = event_manager_day_mask(cal_view_year, cal_view_month),
                .cal_show_list     = cal_show_list,
                .cal_event_count   = event_manager_count(),
                .time_edit         = time_edit,
            };

            int ev_preview = (state.cal_event_count < 3) ? state.cal_event_count : 3;
            for (int i = 0; i < ev_preview; i++) {
                const calendar_event_t *ev = event_manager_get(i);
                if (ev != NULL) {
                    state.cal_events[i].month  = ev->month;
                    state.cal_events[i].day    = ev->day;
                    state.cal_events[i].hour   = ev->hour;
                    state.cal_events[i].minute = ev->minute;
                    strlcpy(state.cal_events[i].title, ev->title,
                            sizeof(state.cal_events[i].title));
                }
            }

            clock_ui_render(&dev, mode, &state);
        }

        frame = (frame + 1) % FRAME_RESET;

        wifi_service_reconnect();
        gcal_sync_service(s_wifi_connected);
        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}
