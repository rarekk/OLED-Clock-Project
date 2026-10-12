#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "sdkconfig.h"
#include "ssd1306.h"

static const char *TAG = "ssd1306";

/* Fallbacks so the component can be built even without the Kconfig symbols. */
#ifndef CONFIG_SSD1306_COLUMN_OFFSET
#define CONFIG_SSD1306_COLUMN_OFFSET 0
#endif
#ifndef CONFIG_SSD1306_SEG_REMAP
#define CONFIG_SSD1306_SEG_REMAP 1
#endif
#ifndef CONFIG_SSD1306_COM_REMAP
#define CONFIG_SSD1306_COM_REMAP 1
#endif

/* Control byte for command (0x00) and data (0x40) transactions. */
#define SSD1306_CTRL_CMD   0x00
#define SSD1306_CTRL_DATA  0x40

/* 8x8 font, one byte per column (bit 0 = top row).
 * Populated for ASCII 0x20 (' ') .. 0x5A ('Z'); everything else is blank.
 * Note: the table has NO lowercase glyphs, so UI strings must be UPPERCASE. */
static const uint8_t font8x8_basic[128][8] = {
    [0 ... 31] = {0},
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x00, 0x00, 0x5f, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['"'] = {0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00},
    ['#'] = {0x14, 0x7f, 0x14, 0x7f, 0x14, 0x00, 0x00, 0x00},
    ['$'] = {0x24, 0x2a, 0x7f, 0x2a, 0x12, 0x00, 0x00, 0x00},
    ['%'] = {0x23, 0x13, 0x08, 0x64, 0x62, 0x00, 0x00, 0x00},
    ['&'] = {0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x00, 0x00},
    ['\''] = {0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x00, 0x1c, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00},
    [')'] = {0x00, 0x41, 0x22, 0x1c, 0x00, 0x00, 0x00, 0x00},
    ['*'] = {0x14, 0x08, 0x3e, 0x08, 0x14, 0x00, 0x00, 0x00},
    ['+'] = {0x08, 0x08, 0x3e, 0x08, 0x08, 0x00, 0x00, 0x00},
    [','] = {0x00, 0x50, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00},
    ['.'] = {0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['/'] = {0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00},
    ['0'] = {0x3e, 0x51, 0x49, 0x45, 0x3e, 0x00, 0x00, 0x00},
    ['1'] = {0x00, 0x42, 0x7f, 0x40, 0x00, 0x00, 0x00, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00, 0x00},
    ['3'] = {0x21, 0x41, 0x45, 0x4b, 0x31, 0x00, 0x00, 0x00},
    ['4'] = {0x18, 0x14, 0x12, 0x7f, 0x10, 0x00, 0x00, 0x00},
    ['5'] = {0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00, 0x00},
    ['6'] = {0x3c, 0x4a, 0x49, 0x49, 0x30, 0x00, 0x00, 0x00},
    ['7'] = {0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00, 0x00},
    ['8'] = {0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00},
    ['9'] = {0x06, 0x49, 0x49, 0x29, 0x1e, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00},
    [';'] = {0x00, 0x56, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['<'] = {0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00},
    ['='] = {0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00},
    ['>'] = {0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00, 0x00},
    ['?'] = {0x02, 0x01, 0x51, 0x09, 0x06, 0x00, 0x00, 0x00},
    ['@'] = {0x32, 0x49, 0x79, 0x41, 0x3e, 0x00, 0x00, 0x00},
    ['A'] = {0x7e, 0x11, 0x11, 0x11, 0x7e, 0x00, 0x00, 0x00},
    ['B'] = {0x7f, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00},
    ['C'] = {0x3e, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00, 0x00},
    ['D'] = {0x7f, 0x41, 0x41, 0x22, 0x1c, 0x00, 0x00, 0x00},
    ['E'] = {0x7f, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00, 0x00},
    ['F'] = {0x7f, 0x09, 0x09, 0x09, 0x01, 0x00, 0x00, 0x00},
    ['G'] = {0x3e, 0x41, 0x49, 0x49, 0x7a, 0x00, 0x00, 0x00},
    ['H'] = {0x7f, 0x08, 0x08, 0x08, 0x7f, 0x00, 0x00, 0x00},
    ['I'] = {0x00, 0x41, 0x7f, 0x41, 0x00, 0x00, 0x00, 0x00},
    ['J'] = {0x20, 0x40, 0x41, 0x3f, 0x01, 0x00, 0x00, 0x00},
    ['K'] = {0x7f, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00},
    ['L'] = {0x7f, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00},
    ['M'] = {0x7f, 0x02, 0x0c, 0x02, 0x7f, 0x00, 0x00, 0x00},
    ['N'] = {0x7f, 0x04, 0x08, 0x10, 0x7f, 0x00, 0x00, 0x00},
    ['O'] = {0x3e, 0x41, 0x41, 0x41, 0x3e, 0x00, 0x00, 0x00},
    ['P'] = {0x7f, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00},
    ['Q'] = {0x3e, 0x41, 0x51, 0x21, 0x5e, 0x00, 0x00, 0x00},
    ['R'] = {0x7f, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00, 0x00},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31, 0x00, 0x00, 0x00},
    ['T'] = {0x01, 0x01, 0x7f, 0x01, 0x01, 0x00, 0x00, 0x00},
    ['U'] = {0x3f, 0x40, 0x40, 0x40, 0x3f, 0x00, 0x00, 0x00},
    ['V'] = {0x1f, 0x20, 0x40, 0x20, 0x1f, 0x00, 0x00, 0x00},
    ['W'] = {0x3f, 0x40, 0x38, 0x40, 0x3f, 0x00, 0x00, 0x00},
    ['X'] = {0x63, 0x14, 0x08, 0x14, 0x63, 0x00, 0x00, 0x00},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07, 0x00, 0x00, 0x00},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x00, 0x00},
};

/* ---- Low level I2C helpers ------------------------------------------- */

static esp_err_t ssd1306_write_cmds(ssd1306_t *dev, const uint8_t *cmds, size_t len)
{
    uint8_t buf[40];
    if (len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = SSD1306_CTRL_CMD;
    memcpy(&buf[1], cmds, len);
    return i2c_master_transmit(dev->i2c_dev, buf, len + 1, -1);
}

static esp_err_t ssd1306_write_data(ssd1306_t *dev, const uint8_t *data, size_t len)
{
    uint8_t buf[SSD1306_WIDTH + 1];   /* fixed size - no VLA on the stack */
    buf[0] = SSD1306_CTRL_DATA;

    while (len > 0) {
        size_t chunk = (len > SSD1306_WIDTH) ? SSD1306_WIDTH : len;
        memcpy(&buf[1], data, chunk);
        esp_err_t err = i2c_master_transmit(dev->i2c_dev, buf, chunk + 1, -1);
        if (err != ESP_OK) {
            return err;
        }
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

/* Point the panel to the start of a page (page addressing mode). */
static esp_err_t ssd1306_set_cursor(ssd1306_t *dev, uint8_t page)
{
    uint8_t cmds[3] = {
        (uint8_t)(0xB0 | (page & 0x07)),
        (uint8_t)(0x00 | (dev->column_offset & 0x0F)),
        (uint8_t)(0x10 | ((dev->column_offset >> 4) & 0x0F)),
    };
    return ssd1306_write_cmds(dev, cmds, sizeof(cmds));
}

/* ---- Public API ------------------------------------------------------- */

esp_err_t ssd1306_init(i2c_master_bus_handle_t bus_handle,
                       const ssd1306_config_t *config,
                       ssd1306_t *dev)
{
    if (bus_handle == NULL || config == NULL || dev == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(dev, 0, sizeof(*dev));
    dev->column_offset = (uint8_t)CONFIG_SSD1306_COLUMN_OFFSET;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr,
        .scl_speed_hz = config->scl_speed_hz,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device(0x%02X) failed: %s",
                 config->i2c_addr, esp_err_to_name(ret));
        return ret;
    }

    const uint8_t init_cmds[] = {
        0xAE,               /* Display OFF */
        0xD5, 0x80,         /* Display clock divide ratio / oscillator */
        0xA8, 0x3F,         /* Multiplex ratio = 64 */
        0xD3, 0x00,         /* Display offset */
        0x40,               /* Display start line = 0 */
        0x8D, 0x14,         /* Charge pump enable */
#if defined(CONFIG_SSD1306_PANEL_SSD1306)
        0x20, 0x02,         /* Memory addressing mode = page (SH1106 has no 0x20 cmd) */
#endif
#if CONFIG_SSD1306_SEG_REMAP
        0xA1,               /* Segment remap */
#else
        0xA0,
#endif
#if CONFIG_SSD1306_COM_REMAP
        0xC8,               /* COM scan direction remapped */
#else
        0xC0,
#endif
        0xDA, 0x12,         /* COM pins hardware config */
        0x81, 0xCF,         /* Contrast */
        0xD9, 0xF1,         /* Pre-charge period */
        0xDB, 0x40,         /* VCOMH deselect level */
        0xA4,               /* Resume to RAM content */
        0xA6,               /* Normal (non-inverted) display */
        0xAF,               /* Display ON */
    };

    ret = ssd1306_write_cmds(dev, init_cmds, sizeof(init_cmds));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init sequence failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ssd1306_clear(dev);
    return ssd1306_update(dev);
}

void ssd1306_clear(ssd1306_t *dev)
{
    memset(dev->buffer, 0, sizeof(dev->buffer));
}

esp_err_t ssd1306_update(ssd1306_t *dev)
{
    esp_err_t first_err = ESP_OK;

    for (uint8_t page = 0; page < SSD1306_PAGES; page++) {
        esp_err_t err = ssd1306_set_cursor(dev, page);
        if (err == ESP_OK) {
            err = ssd1306_write_data(dev, &dev->buffer[page * SSD1306_WIDTH], SSD1306_WIDTH);
        }
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }

    if (first_err != ESP_OK) {
        ESP_LOGW(TAG, "framebuffer update failed: %s", esp_err_to_name(first_err));
    }
    return first_err;
}

esp_err_t ssd1306_set_contrast(ssd1306_t *dev, uint8_t contrast)
{
    uint8_t cmds[2] = { 0x81, contrast };
    return ssd1306_write_cmds(dev, cmds, sizeof(cmds));
}

esp_err_t ssd1306_display_on(ssd1306_t *dev, bool on)
{
    uint8_t cmd = on ? 0xAF : 0xAE;
    return ssd1306_write_cmds(dev, &cmd, 1);
}

/* ---- Pixel primitives ------------------------------------------------- */

void ssd1306_draw_pixel(ssd1306_t *dev, int x, int y, bool color)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    if (color) {
        dev->buffer[x + (y / 8) * SSD1306_WIDTH] |= (1 << (y % 8));
    } else {
        dev->buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
    }
}

void ssd1306_draw_line(ssd1306_t *dev, int x0, int y0, int x1, int y1, bool color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        ssd1306_draw_pixel(dev, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void ssd1306_draw_rect(ssd1306_t *dev, int x, int y, int w, int h, bool color)
{
    if (w <= 0 || h <= 0) return;
    ssd1306_draw_line(dev, x, y, x + w - 1, y, color);
    ssd1306_draw_line(dev, x, y + h - 1, x + w - 1, y + h - 1, color);
    ssd1306_draw_line(dev, x, y, x, y + h - 1, color);
    ssd1306_draw_line(dev, x + w - 1, y, x + w - 1, y + h - 1, color);
}

void ssd1306_fill_rect(ssd1306_t *dev, int x, int y, int w, int h, bool color)
{
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            ssd1306_draw_pixel(dev, x + xx, y + yy, color);
        }
    }
}

void ssd1306_draw_circle(ssd1306_t *dev, int x0, int y0, int r, bool color)
{
    if (r < 0) return;
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;

    ssd1306_draw_pixel(dev, x0, y0 + r, color);
    ssd1306_draw_pixel(dev, x0, y0 - r, color);
    ssd1306_draw_pixel(dev, x0 + r, y0, color);
    ssd1306_draw_pixel(dev, x0 - r, y0, color);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        ssd1306_draw_pixel(dev, x0 + x, y0 + y, color);
        ssd1306_draw_pixel(dev, x0 - x, y0 + y, color);
        ssd1306_draw_pixel(dev, x0 + x, y0 - y, color);
        ssd1306_draw_pixel(dev, x0 - x, y0 - y, color);
        ssd1306_draw_pixel(dev, x0 + y, y0 + x, color);
        ssd1306_draw_pixel(dev, x0 - y, y0 + x, color);
        ssd1306_draw_pixel(dev, x0 + y, y0 - x, color);
        ssd1306_draw_pixel(dev, x0 - y, y0 - x, color);
    }
}

void ssd1306_fill_circle(ssd1306_t *dev, int x0, int y0, int r, bool color)
{
    if (r < 0) return;
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) {
                ssd1306_draw_pixel(dev, x0 + x, y0 + y, color);
            }
        }
    }
}

/* ---- Text ------------------------------------------------------------- */

void ssd1306_draw_char_size(ssd1306_t *dev, int x, int y, char c, int scale, bool invert)
{
    if ((uint8_t)c > 127) return;
    if (scale < 1) scale = 1;

    for (int i = 0; i < 8; i++) {
        uint8_t line = font8x8_basic[(uint8_t)c][i];
        for (int j = 0; j < 8; j++) {
            bool on = (line >> j) & 0x01;
            /* invert => inverse video: the whole cell is drawn, glyph in bg. */
            bool color = invert ? !on : true;
            if (!invert && !on) continue;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    ssd1306_draw_pixel(dev, x + i * scale + sx, y + j * scale + sy, color);
                }
            }
        }
    }
}

void ssd1306_draw_char(ssd1306_t *dev, int x, int y, char c, bool invert)
{
    ssd1306_draw_char_size(dev, x, y, c, 1, invert);
}

void ssd1306_draw_string_size(ssd1306_t *dev, int x, int y, const char *str, int scale, bool invert)
{
    if (scale < 1) scale = 1;
    while (*str) {
        ssd1306_draw_char_size(dev, x, y, *str, scale, invert);
        x += 8 * scale;
        str++;
    }
}

void ssd1306_draw_string(ssd1306_t *dev, int x, int y, const char *str, bool invert)
{
    ssd1306_draw_string_size(dev, x, y, str, 1, invert);
}
