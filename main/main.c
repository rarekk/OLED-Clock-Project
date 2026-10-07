#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "driver/i2c_master.h"
#include "ssd1306.h"
#include "clock_ui.h"

static const char *TAG = "MAIN";

// ================= ตั้งค่า Wi-Fi =================
#define WIFI_SSID      "Ginger"
#define WIFI_PASS      "rarekk160949"

// ================= กำหนดขา GPIO ของปุ่มกด =================
#define BTN_MODE_PIN    GPIO_NUM_13
#define BTN_UP_PIN      GPIO_NUM_12
#define BTN_DOWN_PIN    GPIO_NUM_14
#define BTN_SELECT_PIN  GPIO_NUM_27

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

typedef struct {
    gpio_num_t pin;
    bool last_state;
    int64_t last_debounce_time;
} button_t;

static button_t btn_mode   = { .pin = BTN_MODE_PIN,   .last_state = true, .last_debounce_time = 0 };
static button_t btn_up     = { .pin = BTN_UP_PIN,     .last_state = true, .last_debounce_time = 0 };
static button_t btn_down   = { .pin = BTN_DOWN_PIN,   .last_state = true, .last_debounce_time = 0 };
static button_t btn_select = { .pin = BTN_SELECT_PIN, .last_state = true, .last_debounce_time = 0 };

static bool is_wifi_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        is_wifi_connected = false;
        esp_wifi_connect();
        ESP_LOGI(TAG, "Reconnecting to Wi-Fi...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        is_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected to Wi-Fi!");
    }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void init_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "ICT-7", 1);
    tzset();
}

void init_buttons(void) {
    uint64_t pin_mask = (1ULL << BTN_MODE_PIN) | (1ULL << BTN_UP_PIN) |
                        (1ULL << BTN_DOWN_PIN) | (1ULL << BTN_SELECT_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

bool is_button_pressed(button_t *btn) {
    bool current_state = gpio_get_level(btn->pin);
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool pressed = false;

    if (current_state != btn->last_state) {
        if ((now_ms - btn->last_debounce_time) > 50) {
            btn->last_debounce_time = now_ms;
            btn->last_state = current_state;
            if (current_state == false) {
                pressed = true;
            }
        }
    }
    return pressed;
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_buttons();
    wifi_init_sta();
    init_sntp();

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = GPIO_NUM_22,
        .sda_io_num = GPIO_NUM_21,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    ssd1306_t dev;
    ESP_ERROR_CHECK(ssd1306_init(bus_handle, &dev));

    ui_mode_t current_mode = UI_MODE_CLOCK;
    int anim_frame = 0;
    int selected_mascot = 0;

    bool sw_running = false;
    uint32_t sw_elapsed_ms = 0;

    time_t now = 0;
    struct tm timeinfo = { 0 };

    ESP_LOGI(TAG, "System running...");

    while (1) {
        // 1. ตรวจสอบปุ่มกด
        if (is_button_pressed(&btn_mode)) {
            current_mode = (current_mode + 1) % UI_MODE_MAX;
            ESP_LOGI(TAG, "Mode switched to: %d", current_mode);
        }

        if (current_mode == UI_MODE_STOPWATCH) {
            if (is_button_pressed(&btn_select)) {
                sw_running = !sw_running;
            }
            if (sw_running) {
                sw_elapsed_ms += 100;
            }
        } else if (current_mode == UI_MODE_MASCOT_SELECT) {
            if (is_button_pressed(&btn_up)) {
                selected_mascot = (selected_mascot + 1) % 3;
            }
            if (is_button_pressed(&btn_down)) {
                selected_mascot = (selected_mascot - 1 + 3) % 3;
            }
        }

        // 2. อ่านเวลา
        time(&now);
        localtime_r(&now, &timeinfo);

        const char *wifi_status = is_wifi_connected ? "NTP OK" : "Connecting";

        // 3. แสดงผลบน OLED
        clock_ui_render_mode(&dev, current_mode, 
                            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                            timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                            wifi_status, anim_frame, sw_elapsed_ms, selected_mascot);

        anim_frame++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}