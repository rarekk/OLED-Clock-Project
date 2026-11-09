/*
 * buzzer.h - Non-blocking PWM / GPIO buzzer driver for ESP32.
 *
 * Supports both passive buzzers (variable frequency tones & melodies via LEDC)
 * and active buzzers (rhythmic pulse patterns), driven asynchronously with
 * esp_timer so UI rendering and button polling never block.
 */
#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialise the LEDC PWM channel and internal sequencer timer on `pin`. */
esp_err_t buzzer_init(gpio_num_t pin, bool active_low);

/** Play a single tone at `freq_hz` for `duration_ms` (non-blocking). */
void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms);

/** Short tactile key-click sound (ignored while the alarm melody is playing). */
void buzzer_click(void);

/** Short two-note confirmation chime. */
void buzzer_confirm(void);

/** Start looping the alarm melody until buzzer_alarm_stop() is called. */
void buzzer_alarm_start(void);

/** Immediately silence the buzzer and stop any active melody. */
void buzzer_alarm_stop(void);

/** True if the repeating alarm melody is currently active. */
bool buzzer_alarm_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_H */
