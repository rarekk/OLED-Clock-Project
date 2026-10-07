#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "ssd1306.h"
#include "clock_ui.h"

// ================= กำหนดขา GPIO ของปุ่มกด =================
#define BTN_MODE_PIN    GPIO_NUM_13
#define BTN_UP_PIN      GPIO_NUM_12
#define BTN_DOWN_PIN    GPIO_NUM_14
#define BTN_SELECT_PIN  GPIO_NUM_27

// โครงสร้างสำหรับจัดการ Debounce ของแต่ละปุ่ม
typedef struct {
    gpio_num_t pin;
    bool last_state;
    int64_t last_debounce_time;
} button_t;

static button_t btn_mode   = { .pin = BTN_MODE_PIN,   .last_state = true, .last_debounce_time = 0 };
static button_t btn_up     = { .pin = BTN_UP_PIN,     .last_state = true, .last_debounce_time = 0 };
static button_t btn_down   = { .pin = BTN_DOWN_PIN,   .last_state = true, .last_debounce_time = 0 };
static button_t btn_select = { .pin = BTN_SELECT_PIN, .last_state = true, .last_debounce_time = 0 };

// ตั้งค่าพินปุ่มกดเป็น Input + Pull-Up
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

// ตรวจจับการกดปุ่ม (จะคืนค่า true เพียงครั้งเดียวต่อการกด 1 ครั้ง)
bool is_button_pressed(button_t *btn) {
    bool current_state = gpio_get_level(btn->pin);
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool pressed = false;

    // สัญญาณเปลี่ยน + เลยระยะเวลา Debounce (50ms)
    if (current_state != btn->last_state) {
        if ((now_ms - btn->last_debounce_time) > 50) {
            btn->last_debounce_time = now_ms;
            btn->last_state = current_state;
            if (current_state == false) { // LOW = ปุ่มถูกกด
                pressed = true;
            }
        }
    }
    return pressed;
}

void app_main(void) {
    init_buttons();

    // ... (ตั้งค่า I2C และ SSD1306 ตามโค้ดเดิมของคุณ) ...
    ssd1306_t dev; // สมมติว่า init dev เรียบร้อยแล้ว

    ui_mode_t current_mode = UI_MODE_CLOCK;
    int anim_frame = 0;
    int selected_mascot = 0;

    // สถานะ Stopwatch
    bool sw_running = false;
    uint32_t sw_elapsed_ms = 0;

    time_t now = 0;
    struct tm timeinfo = { 0 };

    while (1) {
        // --- 1. ตรวจสอบการกดปุ่ม MODE เพื่อเปลี่ยนโหมด ---
        if (is_button_pressed(&btn_mode)) {
            current_mode = (current_mode + 1) % UI_MODE_MAX;
            ESP_LOGI("MAIN", "Switched Mode to: %d", current_mode);
        }

        // --- 2. การทำงานตามแต่ละโหมด ---
        if (current_mode == UI_MODE_STOPWATCH) {
            if (is_button_pressed(&btn_select)) {
                sw_running = !sw_running; // กด SELECT เพื่อ Start/Stop
            }
            if (sw_running) {
                sw_elapsed_ms += 100; // เพิ่มเวลาตาม Tick delay (100ms)
            }
        } else if (current_mode == UI_MODE_MASCOT_SELECT) {
            if (is_button_pressed(&btn_up)) {
                selected_mascot = (selected_mascot + 1) % 3;
            }
            if (is_button_pressed(&btn_down)) {
                selected_mascot = (selected_mascot - 1 + 3) % 3;
            }
        }

        // --- 3. ดึงเวลาจริง ---
        time(&now);
        localtime_r(&now, &timeinfo);

        // --- 4. Render หน้าจอ ---
        clock_ui_render_mode(&dev, current_mode, 
                            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                            timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                            "NTP OK", anim_frame, sw_elapsed_ms, selected_mascot);

        anim_frame++;
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay Loop ละ 100ms
    }
}