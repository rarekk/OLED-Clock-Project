#include <stdbool.h>
#include <stddef.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "buzzer.h"

static const char *TAG = "buzzer";

#define BUZZER_LEDC_TIMER   LEDC_TIMER_0
#define BUZZER_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_DUTY_RES     LEDC_TIMER_10_BIT
#define BUZZER_DUTY_ON      512   /* 50% duty cycle for maximum piezo volume */

typedef struct {
    uint16_t freq_hz;       /* 0 = rest (silence) */
    uint16_t duration_ms;
} note_t;

/*
 * Pokemon Center Main Theme (Junichi Masuda) arranged for ESP32 LEDC piezo buzzer.
 * Notes in Octave 5-6 (D Major) + the iconic 5-note Pokeball Healing Jingle.
 */
static const note_t ALARM_MELODY[] = {
    /* Phrase 1: D6 - A5 - D6 - A6 - G6 - F#6 - E6 - C#6 */
    { 1175, 150 }, {    0,  20 }, /* D6  */
    {  880, 150 }, {    0,  20 }, /* A5  */
    { 1175, 150 }, {    0,  20 }, /* D6  */
    { 1760, 300 }, {    0,  40 }, /* A6  */
    { 1568, 300 }, {    0,  40 }, /* G6  */
    { 1480, 150 }, {    0,  20 }, /* F#6 */
    { 1319, 150 }, {    0,  20 }, /* E6  */
    { 1109, 420 }, {    0, 180 }, /* C#6 */

    /* Phrase 2: C#6 - A5 - C#6 - F#6 - E6 - C#6 - D6 - F#6 - A6 */
    { 1109, 150 }, {    0,  20 }, /* C#6 */
    {  880, 150 }, {    0,  20 }, /* A5  */
    { 1109, 150 }, {    0,  20 }, /* C#6 */
    { 1480, 300 }, {    0,  40 }, /* F#6 */
    { 1319, 300 }, {    0,  40 }, /* E6  */
    { 1109, 150 }, {    0,  20 }, /* C#6 */
    { 1175, 150 }, {    0,  20 }, /* D6  */
    { 1480, 150 }, {    0,  20 }, /* F#6 */
    { 1760, 420 }, {    0, 180 }, /* A6  */

    /* Phrase 3: D6 - A5 - D6 - A6 - G6 - F#6 - E6 - G6 */
    { 1175, 150 }, {    0,  20 }, /* D6  */
    {  880, 150 }, {    0,  20 }, /* A5  */
    { 1175, 150 }, {    0,  20 }, /* D6  */
    { 1760, 300 }, {    0,  40 }, /* A6  */
    { 1568, 300 }, {    0,  40 }, /* G6  */
    { 1480, 150 }, {    0,  20 }, /* F#6 */
    { 1319, 150 }, {    0,  20 }, /* E6  */
    { 1568, 420 }, {    0, 120 }, /* G6  */

    /* Phrase 4 (Resolution): F#6 - E6 - D6 - C#6 - D6 */
    { 1480, 150 }, {    0,  20 }, /* F#6 */
    { 1319, 150 }, {    0,  20 }, /* E6  */
    { 1175, 150 }, {    0,  20 }, /* D6  */
    { 1109, 150 }, {    0,  20 }, /* C#6 */
    { 1175, 450 }, {    0, 250 }, /* D6  */

    /* Iconic 5-note Pokemon Center Healing Jingle: B6 - B6 - B6 - G#6 - E7 */
    { 1976, 130 }, {    0,  40 }, /* B6  */
    { 1976, 130 }, {    0,  40 }, /* B6  */
    { 1976, 130 }, {    0,  40 }, /* B6  */
    { 1661, 130 }, {    0,  40 }, /* G#6 */
    { 2637, 380 }, {    0, 400 }, /* E7  */
};

/* Pokemon Center 5-note Healing Jingle for confirmation / boot */
static const note_t CONFIRM_MELODY[] = {
    { 1976, 110 }, {    0,  30 }, /* B6  */
    { 1976, 110 }, {    0,  30 }, /* B6  */
    { 1976, 110 }, {    0,  30 }, /* B6  */
    { 1661, 110 }, {    0,  30 }, /* G#6 */
    { 2637, 260 },                /* E7  */
};

static bool              s_initialized = false;
static bool              s_active_low  = false;
static bool              s_alarm_loop  = false;
static const note_t     *s_seq         = NULL;
static size_t            s_seq_len     = 0;
static size_t            s_seq_idx     = 0;
static esp_timer_handle_t s_timer      = NULL;

static void hw_set_tone(uint32_t freq_hz)
{
    if (!s_initialized) {
        return;
    }
    if (freq_hz == 0) {
        uint32_t idle_duty = s_active_low ? ((1 << BUZZER_DUTY_RES) - 1) : 0;
        ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, idle_duty);
        ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
        return;
    }

    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_ON);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

static void timer_cb(void *arg)
{
    (void)arg;

    if (s_seq == NULL || s_seq_len == 0) {
        hw_set_tone(0);
        return;
    }

    if (s_seq_idx >= s_seq_len) {
        if (s_alarm_loop) {
            s_seq_idx = 0;
        } else {
            s_seq = NULL;
            s_seq_len = 0;
            hw_set_tone(0);
            return;
        }
    }

    note_t n = s_seq[s_seq_idx++];
    hw_set_tone(n.freq_hz);
    esp_timer_start_once(s_timer, (uint64_t)n.duration_ms * 1000ULL);
}

esp_err_t buzzer_init(gpio_num_t pin, bool active_low)
{
    if (pin < 0) {
        ESP_LOGI(TAG, "buzzer disabled (pin < 0)");
        return ESP_OK;
    }

    s_active_low = active_low;

    ledc_timer_config_t tcfg = {
        .speed_mode      = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_DUTY_RES,
        .timer_num       = BUZZER_LEDC_TIMER,
        .freq_hz         = 2000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    uint32_t idle_duty = active_low ? ((1 << BUZZER_DUTY_RES) - 1) : 0;
    ledc_channel_config_t ccfg = {
        .gpio_num   = pin,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel    = BUZZER_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BUZZER_LEDC_TIMER,
        .duty       = idle_duty,
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_timer_create_args_t targs = {
        .callback = &timer_cb,
        .name     = "buzzer_seq",
    };
    err = esp_timer_create(&targs, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    hw_set_tone(0);
    ESP_LOGI(TAG, "initialized on GPIO %d (%s)", (int)pin,
             active_low ? "active-low" : "active-high");
    return ESP_OK;
}

void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms)
{
    if (!s_initialized || s_alarm_loop) {
        return;
    }
    esp_timer_stop(s_timer);
    s_seq = NULL;
    s_seq_len = 0;
    hw_set_tone(freq_hz);
    esp_timer_start_once(s_timer, (uint64_t)duration_ms * 1000ULL);
}

void buzzer_click(void)
{
    buzzer_tone(2400, 15);
}

void buzzer_confirm(void)
{
    if (!s_initialized || s_alarm_loop) {
        return;
    }
    esp_timer_stop(s_timer);
    s_seq = CONFIRM_MELODY;
    s_seq_len = sizeof(CONFIRM_MELODY) / sizeof(CONFIRM_MELODY[0]);
    s_seq_idx = 0;
    timer_cb(NULL);
}

void buzzer_alarm_start(void)
{
    if (!s_initialized) {
        return;
    }
    if (s_alarm_loop) {
        return;   /* already playing */
    }
    esp_timer_stop(s_timer);
    s_alarm_loop = true;
    s_seq = ALARM_MELODY;
    s_seq_len = sizeof(ALARM_MELODY) / sizeof(ALARM_MELODY[0]);
    s_seq_idx = 0;
    timer_cb(NULL);
    ESP_LOGI(TAG, "alarm started");
}

void buzzer_alarm_stop(void)
{
    if (!s_initialized) {
        return;
    }
    esp_timer_stop(s_timer);
    s_alarm_loop = false;
    s_seq = NULL;
    s_seq_len = 0;
    s_seq_idx = 0;
    hw_set_tone(0);
}

bool buzzer_alarm_is_playing(void)
{
    return s_alarm_loop;
}
