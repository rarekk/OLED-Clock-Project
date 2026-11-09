#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "clock_ui.h"

/*
 * Upgraded OLED Clock UI:
 *   - 4 cute 24x24 pixel-art mascots (NEKO, BMO, BOO, USAGI) with multi-frame
 *     animations (blinking, ear/tail movement, floating particles, vertical bounce).
 *   - Dual Clock Face on UI_MODE_CLOCK:
 *       1) Modern Digital Clock with top status bar, seconds badge, 60s progress bar
 *          and animated mascot companion.
 *       2) Creative Analog Chronograph dial (60-step integer trig table) with
 *          thick hour hand, minute hand, sweeping second hand, digital readout
 *          and mascot companion.
 *   - Dedicated Daily Alarm screen (UI_MODE_ALARM) operable with the 4 buttons.
 *   - Stopwatch with Lap/Split recording.
 *   - Interactive Calendar with month browsing and on-screen Event List toggle.
 */

static const char *const MASCOT_NAMES[CLOCK_UI_MASCOT_COUNT] = {
    "NEKO", "BMO", "BOO", "USAGI"
};
static const char *const WEEKDAYS[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

/* ---------------- 8x8 Mini Status Icons ---------------- */

static const uint8_t ICON_BELL_8X8[8] = {
    0x18, 0x3C, 0x3C, 0x3C, 0x7E, 0x7E, 0x00, 0x18
};

static const uint8_t ICON_WIFI_8X8[8] = {
    0x3C, 0x42, 0x99, 0x24, 0x42, 0x18, 0x18, 0x00
};

static const uint8_t ICON_HEART_8X8[8] = {
    0x00, 0x66, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00
};

static const uint8_t ICON_NOTE_8X8[8] = {
    0x0E, 0x0A, 0x0A, 0x0A, 0x6A, 0x7E, 0x60, 0x00
};

static const uint8_t ICON_STAR_8X8[8] = {
    0x10, 0x10, 0x54, 0x38, 0x54, 0x10, 0x10, 0x00
};

/* ---------------- 24x24 Pixel-Art Mascot Sprites ---------------- */

/* 0: NEKO (Cute Pixel Cat - 24x24, 3 bytes/row) */
static const uint8_t SPRITE_NEKO_OPEN[24 * 3] = {
    0x00, 0x00, 0x00,
    0x18, 0x00, 0x18,
    0x3C, 0x00, 0x3C,
    0x7E, 0x00, 0x7E,
    0x7F, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF,
    0xC0, 0x00, 0x03,
    0x80, 0x00, 0x01,
    0x8E, 0x00, 0x71,
    0x9F, 0x00, 0xF9,
    0x9B, 0x00, 0xD9,
    0x8E, 0x00, 0x71,
    0xE0, 0x18, 0x07,
    0x98, 0x24, 0x19,
    0x80, 0xC3, 0x01,
    0xE0, 0x00, 0x07,
    0x60, 0x00, 0x06,
    0x3F, 0xFF, 0xFC,
    0x1F, 0xFF, 0xF8,
    0x18, 0x00, 0x1E,
    0x30, 0x3C, 0x0F,
    0x30, 0x66, 0x0B,
    0x3F, 0xC3, 0xFE,
    0x00, 0x00, 0x00,
};

static const uint8_t SPRITE_NEKO_BLINK[24 * 3] = {
    0x00, 0x00, 0x00,
    0x18, 0x00, 0x18,
    0x3C, 0x00, 0x3C,
    0x7E, 0x00, 0x7E,
    0x7F, 0xFF, 0xFE,
    0xFF, 0xFF, 0xFF,
    0xC0, 0x00, 0x03,
    0x80, 0x00, 0x01,
    0x80, 0x00, 0x01,
    0x80, 0x00, 0x01,
    0x9F, 0x00, 0xF9,
    0x80, 0x00, 0x01,
    0xE0, 0x18, 0x07,
    0x98, 0x24, 0x19,
    0x80, 0xC3, 0x01,
    0xE0, 0x00, 0x07,
    0x60, 0x00, 0x06,
    0x3F, 0xFF, 0xFC,
    0x1F, 0xFF, 0xF8,
    0x18, 0x00, 0x18,
    0x30, 0x3C, 0x38,
    0x30, 0x66, 0x2C,
    0x3F, 0xC3, 0xFC,
    0x00, 0x00, 0x00,
};

/* 1: BMO (Cute Cyber Console Robot - 24x24) */
static const uint8_t SPRITE_BMO_OPEN[24 * 3] = {
    0x00, 0x18, 0x00,
    0x00, 0x3C, 0x00,
    0x00, 0x18, 0x00,
    0x1F, 0xFF, 0xF8,
    0x3F, 0xFF, 0xFC,
    0x30, 0x00, 0x0C,
    0x37, 0xFF, 0xEC,
    0x34, 0x00, 0x2C,
    0x35, 0x81, 0xAC,
    0x35, 0x81, 0xAC,
    0x34, 0x00, 0x2C,
    0x34, 0x3C, 0x2C,
    0x34, 0x00, 0x2C,
    0x37, 0xFF, 0xEC,
    0x30, 0x00, 0x0C,
    0x73, 0xFF, 0xCE,
    0xF0, 0x00, 0x0F,
    0xB1, 0x80, 0x6D,
    0x33, 0xC0, 0xEC,
    0x31, 0x80, 0x6C,
    0x30, 0x00, 0x0C,
    0x3F, 0xFF, 0xFC,
    0x0C, 0x00, 0x30,
    0x1C, 0x00, 0x38,
};

static const uint8_t SPRITE_BMO_BLINK[24 * 3] = {
    0x00, 0x18, 0x00,
    0x00, 0x3C, 0x00,
    0x00, 0x18, 0x00,
    0x1F, 0xFF, 0xF8,
    0x3F, 0xFF, 0xFC,
    0x30, 0x00, 0x0C,
    0x37, 0xFF, 0xEC,
    0x34, 0x00, 0x2C,
    0x34, 0x00, 0x2C,
    0x35, 0xC3, 0xAC,
    0x34, 0x00, 0x2C,
    0x34, 0x7E, 0x2C,
    0x34, 0x3C, 0x2C,
    0x37, 0xFF, 0xEC,
    0x30, 0x00, 0x0C,
    0x33, 0xFF, 0xCC,
    0x70, 0x00, 0x0E,
    0xF1, 0x80, 0x6F,
    0xB3, 0xC0, 0xED,
    0x31, 0x80, 0x6C,
    0x30, 0x00, 0x0C,
    0x3F, 0xFF, 0xFC,
    0x0C, 0x00, 0x30,
    0x1C, 0x00, 0x38,
};

/* 2: BOO (Kawaii Floating Ghost - 24x24) */
static const uint8_t SPRITE_BOO_OPEN[24 * 3] = {
    0x00, 0x7E, 0x00,
    0x03, 0xFF, 0xC0,
    0x07, 0xFF, 0xE0,
    0x0C, 0x00, 0x30,
    0x18, 0x00, 0x18,
    0x10, 0x00, 0x08,
    0x30, 0x00, 0x0C,
    0x23, 0x81, 0xC4,
    0x27, 0xC3, 0xE4,
    0x26, 0xC3, 0x64,
    0x63, 0x81, 0xC6,
    0xFD, 0x00, 0xBF,
    0xC0, 0x3C, 0x03,
    0xC0, 0x3C, 0x03,
    0x60, 0x18, 0x06,
    0x20, 0x00, 0x04,
    0x20, 0x00, 0x04,
    0x30, 0x00, 0x0C,
    0x30, 0x00, 0x0C,
    0x33, 0x18, 0xCC,
    0x3F, 0xBD, 0xFC,
    0x1E, 0x7E, 0x78,
    0x0C, 0x3C, 0x30,
    0x00, 0x00, 0x00,
};

static const uint8_t SPRITE_BOO_BLINK[24 * 3] = {
    0x00, 0x7E, 0x00,
    0x03, 0xFF, 0xC0,
    0x07, 0xFF, 0xE0,
    0x0C, 0x00, 0x30,
    0x18, 0x00, 0x18,
    0x10, 0x00, 0x08,
    0x30, 0x00, 0x0C,
    0x20, 0x00, 0x04,
    0x27, 0xC3, 0xE4,
    0x20, 0x00, 0x04,
    0x6D, 0x00, 0xB6,
    0xC0, 0x18, 0x03,
    0xC0, 0x3C, 0x03,
    0x60, 0x18, 0x06,
    0x20, 0x00, 0x04,
    0x20, 0x00, 0x04,
    0x30, 0x00, 0x0C,
    0x30, 0x00, 0x0C,
    0x31, 0x81, 0x8C,
    0x3B, 0xDB, 0xDC,
    0x1F, 0x7E, 0xF8,
    0x0E, 0x3C, 0x70,
    0x04, 0x18, 0x20,
    0x00, 0x00, 0x00,
};

/* 3: USAGI (Cute Long-Eared Pixel Bunny - 24x24) */
static const uint8_t SPRITE_USAGI_OPEN[24 * 3] = {
    0x07, 0x00, 0xE0,
    0x0F, 0x81, 0xF0,
    0x0D, 0x81, 0xB0,
    0x0D, 0x81, 0xB0,
    0x0D, 0x81, 0xB0,
    0x0F, 0x81, 0xF0,
    0x07, 0xFF, 0xE0,
    0x1F, 0xFF, 0xF8,
    0x38, 0x00, 0x1C,
    0x30, 0x00, 0x0C,
    0x67, 0x00, 0xE6,
    0x6F, 0x81, 0xF6,
    0x6D, 0x81, 0xB6,
    0x67, 0x00, 0xE6,
    0x78, 0x18, 0x1E,
    0x60, 0x3C, 0x06,
    0x30, 0x66, 0x0C,
    0x38, 0x00, 0x1C,
    0x1F, 0xFF, 0xF8,
    0x0C, 0x00, 0x30,
    0x18, 0x3C, 0x18,
    0x18, 0x66, 0x18,
    0x1F, 0xC3, 0xF8,
    0x00, 0x00, 0x00,
};

static const uint8_t SPRITE_USAGI_BLINK[24 * 3] = {
    0x03, 0x80, 0x70,
    0x07, 0xC0, 0xF8,
    0x06, 0xC1, 0xD8,
    0x0D, 0x81, 0xB0,
    0x0D, 0x81, 0xB0,
    0x0F, 0x81, 0xF0,
    0x07, 0xFF, 0xE0,
    0x1F, 0xFF, 0xF8,
    0x38, 0x00, 0x1C,
    0x30, 0x00, 0x0C,
    0x60, 0x00, 0x06,
    0x6F, 0x81, 0xF6,
    0x60, 0x00, 0x06,
    0x78, 0x18, 0x1E,
    0x60, 0x3C, 0x06,
    0x30, 0x66, 0x0C,
    0x38, 0x00, 0x1C,
    0x1F, 0xFF, 0xF8,
    0x0C, 0x00, 0x30,
    0x18, 0x3C, 0x18,
    0x18, 0x66, 0x18,
    0x1F, 0xC3, 0xF8,
    0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};

/* Draw one of the 4 pixel-art mascots at (ox, oy) within a 32x32 box. */
static void draw_mascot(ssd1306_t *dev, int ox, int oy, int index, int frame, bool excited)
{
    int idx = ((index % CLOCK_UI_MASCOT_COUNT) + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT;
    bool blink = ((frame % 24) == 0) || ((frame % 24) == 1);
    bool alt_pose = ((frame / 6) % 2) != 0;

    /* Gentle vertical bobbing (or rapid jumping when excited/alarm ringing) */
    int dy = 0;
    if (excited) {
        dy = (frame % 2 == 0) ? -2 : 1;
    } else {
        dy = ((frame / 5) % 2 == 0) ? 0 : 1;
    }

    const uint8_t *sprite = NULL;
    const uint8_t *particle = NULL;

    switch (idx) {
    case 0: /* NEKO */
        sprite = (blink || (excited && alt_pose)) ? SPRITE_NEKO_BLINK : SPRITE_NEKO_OPEN;
        particle = ICON_HEART_8X8;
        break;
    case 1: /* BMO */
        sprite = (blink || (excited && alt_pose)) ? SPRITE_BMO_BLINK : SPRITE_BMO_OPEN;
        particle = ICON_NOTE_8X8;
        break;
    case 2: /* BOO */
        sprite = (blink || alt_pose) ? SPRITE_BOO_BLINK : SPRITE_BOO_OPEN;
        particle = ICON_STAR_8X8;
        break;
    default: /* USAGI */
        sprite = (blink || alt_pose) ? SPRITE_USAGI_BLINK : SPRITE_USAGI_OPEN;
        particle = ICON_HEART_8X8;
        break;
    }

    /* Draw the 24x24 pixel-art mascot centered inside the 32x32 slot */
    ssd1306_draw_bitmap(dev, ox + 4, oy + 4 + dy, 24, 24, sprite, true);

    /* Animated floating particle in the top-right corner */
    if (particle != NULL && ((frame / 4) % 3 != 0)) {
        int py = oy + ((frame / 4) % 2 == 0 ? 0 : 2);
        ssd1306_draw_bitmap(dev, ox + 24, py, 8, 8, particle, true);
    }

    /* Subtle floor shadow under the mascot */
    ssd1306_draw_line(dev, ox + 8, oy + 30, ox + 23, oy + 30, true);
}

const char *clock_ui_mascot_name(int index)
{
    int n = ((index % CLOCK_UI_MASCOT_COUNT) + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT;
    return MASCOT_NAMES[n];
}

/* ---------------- Analog Clock Trigonometry Table ---------------- */

/* sin(angle) * 100 for 60 steps around the circle (0..59 minutes/seconds). */
static const int8_t SIN60[60] = {
       0,   10,   21,   31,   41,   50,   59,   67,   74,   81,
      87,   91,   95,   98,   99,  100,   99,   98,   95,   91,
      87,   81,   74,   67,   59,   50,   41,   31,   21,   10,
       0,  -10,  -21,  -31,  -41,  -50,  -59,  -67,  -74,  -81,
     -87,  -91,  -95,  -98,  -99, -100,  -99,  -98,  -95,  -91,
     -87,  -81,  -74,  -67,  -59,  -50,  -41,  -31,  -21,  -10
};

static int trig_sin(int step60)
{
    int idx = ((step60 % 60) + 60) % 60;
    return SIN60[idx];
}

static int trig_cos(int step60)
{
    return trig_sin(step60 + 15);
}

/* Draw a clock hand from center (cx, cy) at angle `step60` (0=12 o'clock) with length `len`. */
static void draw_clock_hand(ssd1306_t *dev, int cx, int cy, int step60, int len, bool thick)
{
    int s = trig_sin(step60);
    int c = trig_cos(step60);
    int x1 = cx + (len * s) / 100;
    int y1 = cy - (len * c) / 100;

    ssd1306_draw_line(dev, cx, cy, x1, y1, true);
    if (thick) {
        ssd1306_draw_line(dev, cx + 1, cy, x1 + 1, y1, true);
        ssd1306_draw_line(dev, cx, cy + 1, x1, y1 + 1, true);
    }
}

/* ---------------- Compact 3x5 digits for Calendar & Badges ---------------- */

static const uint8_t small_digits[10][5] = {
    { 0x7, 0x5, 0x5, 0x5, 0x7 }, /* 0 */
    { 0x2, 0x6, 0x2, 0x2, 0x7 }, /* 1 */
    { 0x7, 0x1, 0x7, 0x4, 0x7 }, /* 2 */
    { 0x7, 0x1, 0x7, 0x1, 0x7 }, /* 3 */
    { 0x5, 0x5, 0x7, 0x1, 0x1 }, /* 4 */
    { 0x7, 0x4, 0x7, 0x1, 0x7 }, /* 5 */
    { 0x7, 0x4, 0x7, 0x5, 0x7 }, /* 6 */
    { 0x7, 0x1, 0x2, 0x2, 0x2 }, /* 7 */
    { 0x7, 0x5, 0x7, 0x5, 0x7 }, /* 8 */
    { 0x7, 0x5, 0x7, 0x1, 0x7 }, /* 9 */
};

static int small_number_width(int value)
{
    return (value >= 10) ? 7 : 3;
}

static void draw_small_number(ssd1306_t *dev, int x, int y, int value, bool color)
{
    char digits[3];
    int n = 0;

    if (value >= 100) {
        value %= 100;
    }
    if (value >= 10) {
        digits[n++] = (char)('0' + (value / 10) % 10);
    }
    digits[n++] = (char)('0' + value % 10);

    for (int d = 0; d < n; d++) {
        const uint8_t *glyph = small_digits[digits[d] - '0'];
        int gx = x + d * 4;
        for (int row = 0; row < 5; row++) {
            uint8_t bits = glyph[row];
            if ((bits >> 2) & 1) ssd1306_draw_pixel(dev, gx,     y + row, color);
            if ((bits >> 1) & 1) ssd1306_draw_pixel(dev, gx + 1, y + row, color);
            if (bits & 1)        ssd1306_draw_pixel(dev, gx + 2, y + row, color);
        }
    }
}

/* Render an IPv4 string ("192.168.1.45") with 3x5 digits so it fits in ~50px. */
static void draw_small_ip(ssd1306_t *dev, int x, int y, const char *ip)
{
    if (ip == NULL) {
        return;
    }
    int cx = x;
    for (const char *p = ip; *p != '\0'; p++) {
        if (*p >= '0' && *p <= '9') {
            const uint8_t *glyph = small_digits[*p - '0'];
            for (int row = 0; row < 5; row++) {
                uint8_t bits = glyph[row];
                if ((bits >> 2) & 1) ssd1306_draw_pixel(dev, cx,     y + row, true);
                if ((bits >> 1) & 1) ssd1306_draw_pixel(dev, cx + 1, y + row, true);
                if (bits & 1)        ssd1306_draw_pixel(dev, cx + 2, y + row, true);
            }
            cx += 4;
        } else if (*p == '.') {
            ssd1306_draw_pixel(dev, cx, y + 4, true);
            cx += 2;
        }
    }
}

static int calendar_days_in_month(int year, int month)
{
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (month < 1 || month > 12) {
        return 30;
    }
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

static int calendar_first_weekday(int year, int month)
{
    int y = year;
    int m = month;
    if (m < 3) {
        m += 12;
        y -= 1;
    }
    int k = y % 100;
    int j = y / 100;
    int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;
}

static void to_upper_ascii(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; src != NULL && src[i] != '\0' && i + 1 < n; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        }
        if (c < 32 || c > 126) {
            c = ' ';
        }
        dst[i] = (char)c;
    }
    dst[i] = '\0';
}

/* ---------------- Screen 1A: Digital Clock + Mascot ---------------- */

static void render_clock_digital(ssd1306_t *dev, const ui_state_t *s)
{
    char buf[32];

    /* Outer frame + top status bar divider + right mascot panel divider */
    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_line(dev, 1, 12, SSD1306_WIDTH - 2, 12, true);
    ssd1306_draw_line(dev, 88, 13, 88, 62, true);

    /* Top Status Bar: Weekday + Date on left, icons on right */
    int wd = ((s->weekday % 7) + 7) % 7;
    snprintf(buf, sizeof(buf), "%s %02u/%02u",
             WEEKDAYS[wd], (unsigned)(s->day % 100), (unsigned)(s->month % 100));
    ssd1306_draw_string(dev, 4, 2, buf, false);

    if (s->alarm_enabled || s->alarm_snoozed) {
        ssd1306_draw_bitmap(dev, 98, 2, 8, 8, ICON_BELL_8X8, true);
    }
    if (s->status && strcmp(s->status, "NTP OK") == 0) {
        ssd1306_draw_bitmap(dev, 114, 2, 8, 8, ICON_WIFI_8X8, true);
    }

    /* Main Digital Time (scale 2 = 16x16 per char) */
    char hh[12], mm[12];
    if (s->time_valid) {
        snprintf(hh, sizeof(hh), "%02u", (unsigned)(s->hours % 24));
        snprintf(mm, sizeof(mm), "%02u", (unsigned)(s->minutes % 60));
    } else {
        strcpy(hh, "--");
        strcpy(mm, "--");
    }

    ssd1306_draw_string_size(dev, 4, 17, hh, 2, false);
    /* Blinking colon separator */
    if ((s->frame % 10) < 6 || !s->time_valid) {
        ssd1306_fill_rect(dev, 38, 21, 3, 3, true);
        ssd1306_fill_rect(dev, 38, 28, 3, 3, true);
    }
    ssd1306_draw_string_size(dev, 44, 17, mm, 2, false);

    /* Seconds in an inverse pill badge beside minutes */
    int sec = s->time_valid ? (s->seconds % 60) : 0;
    ssd1306_fill_rect(dev, 76, 17, 10, 9, true);
    draw_small_number(dev, 78, 19, sec, false);

    /* Smooth 60-second progress bar (y = 38..42) */
    ssd1306_draw_rect(dev, 4, 38, 80, 5, true);
    int bar_w = s->time_valid ? ((sec * 78) / 59) : 0;
    if (bar_w > 0) {
        ssd1306_fill_rect(dev, 5, 39, bar_w, 3, true);
    }

    /* Bottom info line: cycle between alarm time, IP address, and network status */
    bool has_ip = (s->ip_addr != NULL && s->ip_addr[0] != '\0');
    int slot = (s->frame / 30) % 3;
    if (s->alarm_snoozed) {
        ssd1306_draw_string(dev, 4, 49, "SNZ ACTIVE", false);
    } else if (slot == 1 && has_ip) {
        ssd1306_draw_string(dev, 4, 49, "IP", false);
        draw_small_ip(dev, 24, 51, s->ip_addr);
    } else if (slot == 2 && s->alarm_enabled) {
        snprintf(buf, sizeof(buf), "AL %02u:%02u", s->alarm_hour, s->alarm_minute);
        ssd1306_draw_string(dev, 4, 49, buf, false);
    } else {
        ssd1306_draw_string(dev, 4, 49, s->status ? s->status : "", false);
    }

    /* Right Panel: Pixel-Art Mascot + Name Tag */
    draw_mascot(dev, 92, 14, s->mascot_index, s->frame, false);
    const char *mname = clock_ui_mascot_name(s->mascot_index);
    int mw = (int)strlen(mname) * 8;
    ssd1306_draw_string(dev, 89 + (38 - mw) / 2, 51, mname, false);
}

/* ---------------- Screen 1B: Creative Analog Clock + Mascot ---------------- */

static void render_clock_analog(ssd1306_t *dev, const ui_state_t *s)
{
    const int cx = 31;
    const int cy = 31;
    const int r  = 29;

    /* Outer watch bezel */
    ssd1306_draw_circle(dev, cx, cy, r, true);
    ssd1306_draw_circle(dev, cx, cy, r - 1, true);

    /* 12 hour markers around the dial */
    for (int h = 0; h < 12; h++) {
        int step = h * 5;
        int sn = trig_sin(step);
        int cs = trig_cos(step);
        int x_out = cx + ((r - 3) * sn) / 100;
        int y_out = cy - ((r - 3) * cs) / 100;

        if (h % 3 == 0) {
            /* Bold cardinal ticks at 12, 3, 6, 9 */
            int x_in = cx + ((r - 7) * sn) / 100;
            int y_in = cy - ((r - 7) * cs) / 100;
            ssd1306_draw_line(dev, x_in, y_in, x_out, y_out, true);
        } else {
            ssd1306_draw_pixel(dev, x_out, y_out, true);
        }
    }

    if (s->time_valid) {
        int sec_step  = s->seconds % 60;
        int min_step  = s->minutes % 60;
        int hour_step = ((s->hours % 12) * 5 + (s->minutes % 60) / 12) % 60;

        /* Hour hand (short & thick), Minute hand (long), Second hand (thin + tip) */
        draw_clock_hand(dev, cx, cy, hour_step, 14, true);
        draw_clock_hand(dev, cx, cy, min_step,  21, false);
        draw_clock_hand(dev, cx, cy, sec_step,  24, false);
    }
    /* Center pivot cap */
    ssd1306_fill_circle(dev, cx, cy, 2, true);

    /* Right Panel: Date, Digital Readout, Mascot, and Alarm Badge */
    ssd1306_draw_rect(dev, 65, 0, 63, 64, true);

    char buf[32];
    int wd = ((s->weekday % 7) + 7) % 7;
    snprintf(buf, sizeof(buf), "%s%02u/%02u",
             WEEKDAYS[wd], (unsigned)(s->day % 100), (unsigned)(s->month % 100));
    ssd1306_draw_string(dev, 68, 3, buf, false);
    ssd1306_draw_line(dev, 66, 12, 126, 12, true);

    if (s->time_valid) {
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 (unsigned)(s->hours % 24),
                 (unsigned)(s->minutes % 60),
                 (unsigned)(s->seconds % 60));
    } else {
        strcpy(buf, "--:--:--");
    }
    ssd1306_draw_string(dev, 68, 15, buf, false);

    /* Mascot on bottom-left of right card + status/alarm on bottom-right */
    draw_mascot(dev, 67, 26, s->mascot_index, s->frame, false);

    if (s->alarm_enabled) {
        ssd1306_draw_bitmap(dev, 106, 30, 8, 8, ICON_BELL_8X8, true);
        draw_small_number(dev, 102, 42, s->alarm_hour, true);
        ssd1306_draw_pixel(dev, 111, 43, true);
        ssd1306_draw_pixel(dev, 111, 45, true);
        draw_small_number(dev, 113, 42, s->alarm_minute, true);
    } else if (s->status && strcmp(s->status, "NTP OK") == 0) {
        ssd1306_draw_bitmap(dev, 106, 36, 8, 8, ICON_WIFI_8X8, true);
    }
}

static void render_clock(ssd1306_t *dev, const ui_state_t *s)
{
    if (s->clock_style == CLOCK_STYLE_ANALOG) {
        render_clock_analog(dev, s);
    } else {
        render_clock_digital(dev, s);
    }
}

/* ---------------- Screen 2: Daily Alarm + Buzzer ---------------- */

static void render_alarm(ssd1306_t *dev, const ui_state_t *s)
{
    char buf[20];

    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_line(dev, 1, 12, SSD1306_WIDTH - 2, 12, true);

    ssd1306_draw_bitmap(dev, 6, 2, 8, 8, ICON_BELL_8X8, true);
    ssd1306_draw_string(dev, 18, 2, "DAILY ALARM", false);

    /* Alarm Time HH:MM (scale 2) */
    snprintf(buf, sizeof(buf), "%02u", s->alarm_hour % 24);
    bool inv_h = (s->alarm_edit == ALARM_EDIT_HOUR);
    ssd1306_draw_string_size(dev, 8, 20, buf, 2, inv_h);

    ssd1306_draw_string_size(dev, 40, 20, ":", 2, false);

    snprintf(buf, sizeof(buf), "%02u", s->alarm_minute % 60);
    bool inv_m = (s->alarm_edit == ALARM_EDIT_MIN);
    ssd1306_draw_string_size(dev, 54, 20, buf, 2, inv_m);

    /* ON / OFF toggle badge */
    bool inv_en = (s->alarm_edit == ALARM_EDIT_ENABLE);
    const char *state_str = s->alarm_enabled ? " ON " : "OFF ";
    ssd1306_draw_rect(dev, 90, 20, 34, 14, true);
    ssd1306_draw_string(dev, 91, 23, state_str, inv_en);

    if (s->alarm_snoozed) {
        ssd1306_draw_string(dev, 8, 40, "SNOOZE 5M ON", false);
    } else {
        ssd1306_draw_string(dev, 8, 40,
                            s->alarm_enabled ? "BUZZER ARMED" : "ALARM MUTED", false);
    }

    /* Bottom 4-button guide */
    ssd1306_draw_line(dev, 1, 51, SSD1306_WIDTH - 2, 51, true);
    if (s->alarm_edit == ALARM_EDIT_NONE) {
        ssd1306_draw_string(dev, 4, 54, "[OK]SET [UP]SW", false);
    } else {
        ssd1306_draw_string(dev, 4, 54, "[UP/DN]+- [OK]NX", false);
    }
}

/* ---------------- Screen 3: Stopwatch + Lap ---------------- */

static void render_stopwatch(ssd1306_t *dev, const ui_state_t *s)
{
    uint32_t total = s->stopwatch_ms / 1000;
    uint32_t mm = (total / 60) % 100;
    uint32_t ss = total % 60;
    uint32_t cs = (s->stopwatch_ms % 1000) / 10;

    char big[24];
    char cs_buf[16];
    snprintf(big, sizeof(big), "%02" PRIu32 ":%02" PRIu32, mm, ss);
    snprintf(cs_buf, sizeof(cs_buf), ".%02" PRIu32, cs);

    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_line(dev, 1, 12, SSD1306_WIDTH - 2, 12, true);
    ssd1306_draw_string(dev, 6, 2, "STOPWATCH", false);
    ssd1306_draw_string(dev, 90, 2, s->stopwatch_running ? "RUN " : "HOLD", s->stopwatch_running);

    ssd1306_draw_string_size(dev, 12, 17, big, 2, false);
    ssd1306_draw_string(dev, 94, 24, cs_buf, false);

    /* Lap split display or progress bar */
    if (s->stopwatch_lap_num > 0) {
        uint32_t ltot = s->stopwatch_lap_ms / 1000;
        uint32_t lmm  = (ltot / 60) % 100;
        uint32_t lss  = ltot % 60;
        uint32_t lcs  = (s->stopwatch_lap_ms % 1000) / 10;
        char lap_str[32];
        snprintf(lap_str, sizeof(lap_str), "L%u %02" PRIu32 ":%02" PRIu32 ".%02" PRIu32,
                 (unsigned)(s->stopwatch_lap_num % 10), lmm, lss, lcs);
        ssd1306_draw_string(dev, 12, 39, lap_str, false);
    } else {
        ssd1306_draw_rect(dev, 12, 40, 104, 5, true);
        int fill = (int)((cs * 102) / 99);
        if (fill > 0) {
            ssd1306_fill_rect(dev, 13, 41, fill, 3, true);
        }
    }

    ssd1306_draw_line(dev, 1, 51, SSD1306_WIDTH - 2, 51, true);
    ssd1306_draw_string(dev, 4, 54, "[OK]GO [UP]LAP", false);
}

/* ---------------- Screen 4: Interactive Calendar & Event List ---------------- */

static void render_calendar(ssd1306_t *dev, const ui_state_t *s)
{
    static const char *const MONTHS[12] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    static const char *const WD_LETTERS[7] = { "S", "M", "T", "W", "T", "F", "S" };

    if (s->cal_show_list) {
        /* On-screen Event List View (toggled with SELECT button) */
        ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
        ssd1306_draw_line(dev, 1, 12, SSD1306_WIDTH - 2, 12, true);
        ssd1306_draw_string(dev, 6, 2, "EVENTS LIST", false);

        if (s->cal_event_count == 0) {
            ssd1306_draw_string(dev, 16, 26, "NO EVENTS YET", false);
        } else {
            int n = (s->cal_event_count < 3) ? s->cal_event_count : 3;
            for (int i = 0; i < n; i++) {
                char line[32];
                char up[10];
                to_upper_ascii(up, s->cal_events[i].title, sizeof(up));
                snprintf(line, sizeof(line), "%02u/%02u %02u:%02u %s",
                         (unsigned)(s->cal_events[i].day % 100),
                         (unsigned)(s->cal_events[i].month % 100),
                         (unsigned)(s->cal_events[i].hour % 100),
                         (unsigned)(s->cal_events[i].minute % 100), up);
                line[15] = '\0';
                ssd1306_draw_string(dev, 4, 16 + i * 11, line, false);
            }
        }
        ssd1306_draw_line(dev, 1, 51, SSD1306_WIDTH - 2, 51, true);
        if (s->ip_addr && s->ip_addr[0] != '\0' && ((s->frame / 25) % 2 == 1)) {
            ssd1306_draw_string(dev, 4, 54, s->ip_addr, false);
        } else {
            ssd1306_draw_string(dev, 4, 54, "[OK]CAL [DN]SYN", false);
        }
        return;
    }

    int month = (s->cal_month >= 1 && s->cal_month <= 12) ? s->cal_month : s->month;
    if (month < 1 || month > 12) month = 1;
    int year = (s->cal_year >= 2020) ? s->cal_year : s->year;

    char header[24];
    snprintf(header, sizeof(header), "%s %04u", MONTHS[month - 1], (unsigned)(year % 10000));
    int hw = (int)strlen(header) * 8;
    ssd1306_draw_string(dev, (88 - hw) / 2, 0, header, false);

    /* Right panel: divider + mascot + event count badge */
    ssd1306_draw_line(dev, 88, 0, 88, 63, true);
    draw_mascot(dev, 92, 8, s->mascot_index, s->frame, false);
    char ev_str[20];
    snprintf(ev_str, sizeof(ev_str), "EV:%d", s->cal_event_count);
    ssd1306_draw_string(dev, 92, 46, ev_str, false);
    ssd1306_draw_string(dev, 92, 55, "[OK]", false);

    /* Weekday header row */
    const int cell_w = 12;
    const int grid_x = 2;
    for (int c = 0; c < 7; c++) {
        int cx = grid_x + c * cell_w + (cell_w - 8) / 2;
        ssd1306_draw_string(dev, cx, 10, WD_LETTERS[c], false);
    }

    int first = calendar_first_weekday(year, month);
    int ndays = calendar_days_in_month(year, month);
    int today = (s->time_valid && year == s->year && month == s->month) ? s->day : -1;

    int row = 0;
    int col = first;
    for (int d = 1; d <= ndays; d++) {
        int cx = grid_x + col * cell_w;
        int cy = 19 + row * 7;
        int w = small_number_width(d);
        int nx = cx + (cell_w - w) / 2;

        if (d == today) {
            ssd1306_fill_rect(dev, nx - 2, cy - 1, w + 4, 6, true);
            draw_small_number(dev, nx, cy, d, false);
        } else {
            draw_small_number(dev, nx, cy, d, true);
        }

        if ((s->event_days >> (d - 1)) & 1u) {
            ssd1306_fill_rect(dev, cx + 5, cy + 5, 2, 2, true);
        }

        col++;
        if (col == 7) {
            col = 0;
            row++;
        }
    }
}

/* ---------------- Screen 5: 4-Mascot Picker ---------------- */

static void render_mascot_select(ssd1306_t *dev, const ui_state_t *s)
{
    char buf[24];

    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_string(dev, 12, 3, "SELECT MASCOT", false);

    for (int i = 0; i < CLOCK_UI_MASCOT_COUNT; i++) {
        int ox = 2 + i * 31;   /* 4 slots across 128px */
        draw_mascot(dev, ox, 15, i, s->frame, i == s->mascot_index);
        if (i == s->mascot_index) {
            ssd1306_draw_rect(dev, ox + 1, 15, 30, 32, true);
        }
    }

    snprintf(buf, sizeof(buf), "< %s %d/%d >",
             clock_ui_mascot_name(s->mascot_index),
             s->mascot_index + 1, CLOCK_UI_MASCOT_COUNT);
    int w = (int)strlen(buf) * 8;
    ssd1306_draw_string(dev, (SSD1306_WIDTH - w) / 2, 52, buf, false);
}

/* ---------------- Screen 6: Manual Time & Date Editor ---------------- */

static void render_set_time(ssd1306_t *dev, const ui_state_t *s)
{
    char buf[16];

    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_line(dev, 1, 12, SSD1306_WIDTH - 2, 12, true);
    ssd1306_draw_string(dev, 6, 2, "SET TIME/DATE", false);

    int hh = (s->hours >= 0 && s->hours < 24) ? s->hours : 0;
    int mm = (s->minutes >= 0 && s->minutes < 60) ? s->minutes : 0;
    int ss = (s->seconds >= 0 && s->seconds < 60) ? s->seconds : 0;
    int dd = (s->day >= 1 && s->day <= 31) ? s->day : 1;
    int mo = (s->month >= 1 && s->month <= 12) ? s->month : 1;
    int yy = (s->year >= 2024 && s->year <= 2099) ? s->year : 2026;

    /* Row 1: TIME HH:MM:SS */
    ssd1306_draw_string(dev, 6, 20, "TIME", false);

    snprintf(buf, sizeof(buf), "%02u", (unsigned)hh);
    ssd1306_draw_string(dev, 48, 20, buf, s->time_edit == TIME_EDIT_HOUR);
    ssd1306_draw_string(dev, 64, 20, ":", false);

    snprintf(buf, sizeof(buf), "%02u", (unsigned)mm);
    ssd1306_draw_string(dev, 72, 20, buf, s->time_edit == TIME_EDIT_MIN);
    ssd1306_draw_string(dev, 88, 20, ":", false);

    snprintf(buf, sizeof(buf), "%02u", (unsigned)ss);
    ssd1306_draw_string(dev, 96, 20, buf, s->time_edit == TIME_EDIT_SEC);

    /* Row 2: DATE DD/MM/YYYY */
    ssd1306_draw_string(dev, 6, 36, "DATE", false);

    snprintf(buf, sizeof(buf), "%02u", (unsigned)dd);
    ssd1306_draw_string(dev, 42, 36, buf, s->time_edit == TIME_EDIT_DAY);
    ssd1306_draw_string(dev, 58, 36, "/", false);

    snprintf(buf, sizeof(buf), "%02u", (unsigned)mo);
    ssd1306_draw_string(dev, 66, 36, buf, s->time_edit == TIME_EDIT_MONTH);
    ssd1306_draw_string(dev, 82, 36, "/", false);

    snprintf(buf, sizeof(buf), "%04u", (unsigned)yy);
    ssd1306_draw_string(dev, 90, 36, buf, s->time_edit == TIME_EDIT_YEAR);

    /* Bottom guide */
    ssd1306_draw_line(dev, 1, 51, SSD1306_WIDTH - 2, 51, true);
    if (s->time_edit == TIME_EDIT_NONE) {
        ssd1306_draw_string(dev, 4, 54, "[OK]SET [DN]NTP", false);
    } else {
        ssd1306_draw_string(dev, 4, 54, "[UP/DN]+- [OK]NX", false);
    }
}

/* ---------------- Entry points ---------------- */

void clock_ui_render(ssd1306_t *dev, ui_mode_t mode, const ui_state_t *state)
{
    if (dev == NULL || state == NULL) {
        return;
    }

    ssd1306_clear(dev);

    switch (mode) {
    case UI_MODE_CLOCK:
        render_clock(dev, state);
        break;
    case UI_MODE_ALARM:
        render_alarm(dev, state);
        break;
    case UI_MODE_STOPWATCH:
        render_stopwatch(dev, state);
        break;
    case UI_MODE_CALENDAR:
        render_calendar(dev, state);
        break;
    case UI_MODE_MASCOT_SELECT:
        render_mascot_select(dev, state);
        break;
    case UI_MODE_SET_TIME:
        render_set_time(dev, state);
        break;
    default:
        break;
    }

    ssd1306_update(dev);
}

void clock_ui_render_splash(ssd1306_t *dev, int mascot_index, int frame)
{
    if (dev == NULL) {
        return;
    }

    ssd1306_clear(dev);
    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_rect(dev, 2, 2, SSD1306_WIDTH - 4, SSD1306_HEIGHT - 4, true);
    ssd1306_draw_string(dev, 24, 6, "OLED CLOCK", false);
    draw_mascot(dev, 48, 17, mascot_index, frame, true);
    ssd1306_draw_rect(dev, 24, 52, 80, 5, true);
    int fill = ((frame + 1) * 78) / 15;
    if (fill > 78) fill = 78;
    ssd1306_fill_rect(dev, 25, 53, fill, 3, true);
    ssd1306_update(dev);
}

void clock_ui_render_event_popup(ssd1306_t *dev, const char *header,
                                 const char *title, int mascot_index, int frame)
{
    if (dev == NULL) {
        return;
    }

    char up_hdr[20];
    char up_title[20];
    to_upper_ascii(up_hdr, header ? header : "! ALARM !", sizeof(up_hdr));
    to_upper_ascii(up_title, title ? title : "WAKE UP", sizeof(up_title));
    up_title[11] = '\0';

    ssd1306_clear(dev);

    /* Flashing double border */
    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    if ((frame / 2) % 2 == 0) {
        ssd1306_draw_rect(dev, 2, 2, SSD1306_WIDTH - 4, SSD1306_HEIGHT - 4, true);
    }

    ssd1306_draw_bitmap(dev, 6, 5, 8, 8, ICON_BELL_8X8, true);
    ssd1306_draw_string(dev, 20, 5, up_hdr, false);
    ssd1306_draw_line(dev, 4, 15, SSD1306_WIDTH - 5, 15, true);

    /* Jumping mascot on the left + message on the right */
    draw_mascot(dev, 4, 17, mascot_index, frame, true);
    ssd1306_draw_string(dev, 38, 28, up_title, false);

    ssd1306_draw_line(dev, 4, 50, SSD1306_WIDTH - 5, 50, true);
    ssd1306_draw_string(dev, 6, 53, "[OK]OFF [UP]SNZ", false);
    ssd1306_update(dev);
}
