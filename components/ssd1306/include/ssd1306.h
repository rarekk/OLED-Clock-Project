#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t buffer[1024]; // 128x64 pixels = 1024 bytes
} ssd1306_t;

esp_err_t ssd1306_init(i2c_master_bus_handle_t bus_handle, ssd1306_t *dev);
void ssd1306_clear(ssd1306_t *dev);
esp_err_t ssd1306_update(ssd1306_t *dev);

void ssd1306_draw_pixel(ssd1306_t *dev, int x, int y, bool color);
void ssd1306_draw_line(ssd1306_t *dev, int x0, int y0, int x1, int y1, bool color);
void ssd1306_draw_rect(ssd1306_t *dev, int x, int y, int w, int h, bool color);

void ssd1306_draw_char(ssd1306_t *dev, int x, int y, char c, bool invert);
void ssd1306_draw_string(ssd1306_t *dev, int x, int y, const char *str, bool invert);

#ifdef __cplusplus
}
#endif

#endif // SSD1306_H