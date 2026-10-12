/*
 * ssd1306.h - Minimal, dependency-light driver for SSD1306 / SH1106
 *             128x64 monochrome OLED panels using the ESP-IDF v5
 *             "i2c_master" (new) driver.
 *
 * The panel controller and orientation are selected through Kconfig
 * (see components/ssd1306/Kconfig). SH1106 (typical 1.3" modules) needs
 * a 2-column offset which is handled transparently.
 */
#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   (SSD1306_HEIGHT / 8)

/** Runtime configuration passed to ssd1306_init(). */
typedef struct {
    uint8_t  i2c_addr;      /*!< 7-bit I2C address, usually 0x3C or 0x3D */
    uint32_t scl_speed_hz;  /*!< I2C SCL frequency in Hz, e.g. 400000 */
} ssd1306_config_t;

/** Convenience initialiser for the common case. */
#define SSD1306_DEFAULT_CONFIG() \
    ((ssd1306_config_t){ .i2c_addr = 0x3C, .scl_speed_hz = 400000 })

/** Device handle. Allocate one per panel (typically on the stack or static). */
typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    uint8_t                 column_offset;                  /*!< 0 = SSD1306, 2 = SH1106 */
    uint8_t                 buffer[SSD1306_WIDTH * SSD1306_PAGES]; /*!< 1024 byte framebuffer */
} ssd1306_t;

/* ---- Lifecycle -------------------------------------------------------- */

/** Add the panel to an existing I2C bus, run the init sequence and clear it. */
esp_err_t ssd1306_init(i2c_master_bus_handle_t bus_handle,
                       const ssd1306_config_t *config,
                       ssd1306_t *dev);

/** Zero the local framebuffer (does not touch the panel until update). */
void ssd1306_clear(ssd1306_t *dev);

/** Push the whole framebuffer to the panel. Returns the first I2C error. */
esp_err_t ssd1306_update(ssd1306_t *dev);

/** Set panel contrast/brightness (0x00 - 0xFF). */
esp_err_t ssd1306_set_contrast(ssd1306_t *dev, uint8_t contrast);

/** Turn the panel on (true) or off (false). */
esp_err_t ssd1306_display_on(ssd1306_t *dev, bool on);

/* ---- Pixel primitives ------------------------------------------------- */

void ssd1306_draw_pixel(ssd1306_t *dev, int x, int y, bool color);
void ssd1306_draw_line(ssd1306_t *dev, int x0, int y0, int x1, int y1, bool color);
void ssd1306_draw_rect(ssd1306_t *dev, int x, int y, int w, int h, bool color);
void ssd1306_fill_rect(ssd1306_t *dev, int x, int y, int w, int h, bool color);
void ssd1306_draw_circle(ssd1306_t *dev, int x0, int y0, int r, bool color);
void ssd1306_fill_circle(ssd1306_t *dev, int x0, int y0, int r, bool color);

/* ---- Text (built-in 8x8 font, ASCII 0x20-0x5A: space..'Z') ------------ */

void ssd1306_draw_char(ssd1306_t *dev, int x, int y, char c, bool invert);
void ssd1306_draw_char_size(ssd1306_t *dev, int x, int y, char c, int scale, bool invert);
void ssd1306_draw_string(ssd1306_t *dev, int x, int y, const char *str, bool invert);
void ssd1306_draw_string_size(ssd1306_t *dev, int x, int y, const char *str, int scale, bool invert);

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_H */
