#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "clock_ui.h"

/*
 * All mascots are drawn procedurally from primitives (lines, rects,
 * circles) instead of hard-coded bitmaps. That keeps the source readable,
 * scales cleanly and lets the same routine animate every mascot.
 *
 * IMPORTANT: the built-in font only contains uppercase ASCII, so every
 * on-screen string in this file is uppercase.
 */

static const char *const MASCOT_NAMES[CLOCK_UI_MASCOT_COUNT] = { "CAT", "ROBOT", "GHOST" };
static const char *const WEEKDAYS[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

/* Blink every ~2 s (frame rate is ~10 fps). */
static bool mascot_eyes_open(int frame)
{
    return (frame % 20) != 0;
}

/* ---------------- Mascot: cat ---------------- */
static void draw_cat(ssd1306_t *dev, int ox, int oy, bool eyes_open)
{
    /* ears */
    ssd1306_draw_line(dev, ox + 7, oy + 9, ox + 10, oy + 3, true);
    ssd1306_draw_line(dev, ox + 10, oy + 3, ox + 14, oy + 8, true);
    ssd1306_draw_line(dev, ox + 18, oy + 8, ox + 22, oy + 3, true);
    ssd1306_draw_line(dev, ox + 22, oy + 3, ox + 25, oy + 9, true);

    /* head */
    ssd1306_draw_circle(dev, ox + 16, oy + 15, 11, true);

    /* whiskers */
    ssd1306_draw_line(dev, ox + 2, oy + 17, ox + 7, oy + 18, true);
    ssd1306_draw_line(dev, ox + 25, oy + 18, ox + 30, oy + 17, true);

    /* eyes */
    if (eyes_open) {
        ssd1306_fill_circle(dev, ox + 12, oy + 13, 2, true);
        ssd1306_fill_circle(dev, ox + 20, oy + 13, 2, true);
    } else {
        ssd1306_draw_line(dev, ox + 10, oy + 13, ox + 14, oy + 13, true);
        ssd1306_draw_line(dev, ox + 18, oy + 13, ox + 22, oy + 13, true);
    }

    /* nose + mouth */
    ssd1306_fill_circle(dev, ox + 16, oy + 18, 1, true);
    ssd1306_draw_line(dev, ox + 16, oy + 19, ox + 13, oy + 22, true);
    ssd1306_draw_line(dev, ox + 16, oy + 19, ox + 19, oy + 22, true);
}

/* ---------------- Mascot: robot ---------------- */
static void draw_robot(ssd1306_t *dev, int ox, int oy, bool eyes_open)
{
    /* antenna */
    ssd1306_draw_line(dev, ox + 16, oy + 6, ox + 16, oy + 2, true);
    ssd1306_fill_circle(dev, ox + 16, oy + 1, 1, true);

    /* ears */
    ssd1306_fill_rect(dev, ox + 2, oy + 12, 3, 6, true);
    ssd1306_fill_rect(dev, ox + 27, oy + 12, 3, 6, true);

    /* head */
    ssd1306_draw_rect(dev, ox + 5, oy + 6, 22, 17, true);

    /* eyes */
    if (eyes_open) {
        ssd1306_fill_rect(dev, ox + 9, oy + 11, 4, 4, true);
        ssd1306_fill_rect(dev, ox + 19, oy + 11, 4, 4, true);
    } else {
        ssd1306_draw_line(dev, ox + 9, oy + 13, ox + 13, oy + 13, true);
        ssd1306_draw_line(dev, ox + 19, oy + 13, ox + 23, oy + 13, true);
    }

    /* mouth */
    ssd1306_draw_line(dev, ox + 10, oy + 18, ox + 13, oy + 20, true);
    ssd1306_draw_line(dev, ox + 13, oy + 20, ox + 19, oy + 20, true);
    ssd1306_draw_line(dev, ox + 19, oy + 20, ox + 22, oy + 18, true);

    /* body */
    ssd1306_draw_rect(dev, ox + 11, oy + 25, 10, 6, true);
}

/* ---------------- Mascot: ghost ---------------- */
static void draw_ghost(ssd1306_t *dev, int ox, int oy, bool eyes_open)
{
    /* rounded head + straight sides */
    ssd1306_draw_circle(dev, ox + 16, oy + 13, 10, true);
    ssd1306_draw_line(dev, ox + 6, oy + 13, ox + 6, oy + 24, true);
    ssd1306_draw_line(dev, ox + 26, oy + 13, ox + 26, oy + 24, true);

    /* wavy bottom */
    ssd1306_draw_line(dev, ox + 6, oy + 24, ox + 9, oy + 27, true);
    ssd1306_draw_line(dev, ox + 9, oy + 27, ox + 12, oy + 24, true);
    ssd1306_draw_line(dev, ox + 12, oy + 24, ox + 16, oy + 27, true);
    ssd1306_draw_line(dev, ox + 16, oy + 27, ox + 20, oy + 24, true);
    ssd1306_draw_line(dev, ox + 20, oy + 24, ox + 23, oy + 27, true);
    ssd1306_draw_line(dev, ox + 23, oy + 27, ox + 26, oy + 24, true);

    /* eyes */
    if (eyes_open) {
        ssd1306_fill_circle(dev, ox + 12, oy + 12, 2, true);
        ssd1306_fill_circle(dev, ox + 20, oy + 12, 2, true);
    } else {
        ssd1306_draw_line(dev, ox + 10, oy + 12, ox + 14, oy + 12, true);
        ssd1306_draw_line(dev, ox + 18, oy + 12, ox + 22, oy + 12, true);
    }

    /* mouth */
    ssd1306_fill_circle(dev, ox + 16, oy + 17, 2, true);
}

static void draw_mascot(ssd1306_t *dev, int ox, int oy, int index, int frame)
{
    bool eyes = mascot_eyes_open(frame);
    switch (((index % CLOCK_UI_MASCOT_COUNT) + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT) {
    case 0:  draw_cat(dev, ox, oy, eyes);   break;
    case 1:  draw_robot(dev, ox, oy, eyes); break;
    default: draw_ghost(dev, ox, oy, eyes); break;
    }
}

const char *clock_ui_mascot_name(int index)
{
    int n = ((index % CLOCK_UI_MASCOT_COUNT) + CLOCK_UI_MASCOT_COUNT) % CLOCK_UI_MASCOT_COUNT;
    return MASCOT_NAMES[n];
}

/* ---------------- Calendar helpers ---------------- */

/* Compact 3x5 digits for the calendar grid. Each row is 3 bits:
 * bit2 = left column, bit1 = middle, bit0 = right. */
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

/* Pixel width of a value drawn with the tiny digits (2 digits use 3+1+3 px). */
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

/* Weekday of the 1st of the month, 0 = Sunday (Zeller's congruence). */
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
    int h = (1 + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7; /* 0 = Saturday */
    return (h + 6) % 7;                                                /* 0 = Sunday */
}

/* ---------------- Screens ---------------- */

static void render_clock(ssd1306_t *dev, const ui_state_t *s)
{
    char buf[24];

    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_line(dev, 88, 1, 88, 62, true);

    int wd = ((s->weekday % 7) + 7) % 7;
    snprintf(buf, sizeof(buf), "%s %02d/%02d", WEEKDAYS[wd], s->day, s->month);
    ssd1306_draw_string(dev, 4, 6, buf, false);

    char t[8];
    if (s->time_valid) {
        snprintf(t, sizeof(t), "%02d:%02d", s->hours % 24, s->minutes % 60);
    } else {
        strcpy(t, "--:--");
    }
    ssd1306_draw_string_size(dev, 4, 18, t, 2, false);

    snprintf(buf, sizeof(buf), ":%02d", s->seconds % 60);
    ssd1306_draw_string(dev, 4, 40, buf, false);

    ssd1306_draw_string(dev, 4, 52, s->status ? s->status : "", false);

    draw_mascot(dev, 92, 16, s->mascot_index, s->frame);
}

static void render_stopwatch(ssd1306_t *dev, const ui_state_t *s)
{
    uint32_t total = s->stopwatch_ms / 1000;
    uint32_t mm = total / 60;
    uint32_t ss = total % 60;
    uint32_t cs = (s->stopwatch_ms % 1000) / 10;

    char big[12];
    char mid[20];

    snprintf(big, sizeof(big), "%02" PRIu32 ":%02" PRIu32, mm, ss);
    snprintf(mid, sizeof(mid), ".%02" PRIu32 "  %s", cs, s->stopwatch_running ? "RUN" : "STOP");

    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    ssd1306_draw_string(dev, 28, 8, "STOPWATCH", false);
    ssd1306_draw_string_size(dev, 24, 20, big, 2, false);

    int w = (int)strlen(mid) * 8;
    ssd1306_draw_string(dev, (SSD1306_WIDTH - w) / 2, 42, mid, false);
    ssd1306_draw_string(dev, 4, 54, "[OK]S/S [DN]RST", false);
}

static void render_calendar(ssd1306_t *dev, const ui_state_t *s)
{
    static const char *const MONTHS[12] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
    static const char *const WD_LETTERS[7] = { "S", "M", "T", "W", "T", "F", "S" };

    int month = (s->month >= 1 && s->month <= 12) ? s->month : 1;
    int year = s->year;

    /* Header over the calendar area: e.g. "OCT 2026" */
    char header[12];
    snprintf(header, sizeof(header), "%s %04d", MONTHS[month - 1], year);
    int hw = (int)strlen(header) * 8;
    ssd1306_draw_string(dev, (88 - hw) / 2, 0, header, false);

    /* Right panel: divider + mascot (32x32) */
    ssd1306_draw_line(dev, 88, 10, 88, 62, true);
    draw_mascot(dev, 92, 16, s->mascot_index, s->frame);

    /* Weekday header row: S M T W T F S */
    const int cell_w = 12;
    const int grid_x = 2;
    for (int c = 0; c < 7; c++) {
        int cx = grid_x + c * cell_w + (cell_w - 8) / 2;
        ssd1306_draw_string(dev, cx, 10, WD_LETTERS[c], false);
    }

    /* Day grid (up to 6 rows) */
    int first = calendar_first_weekday(year, month);
    int ndays = calendar_days_in_month(year, month);
    int today = s->time_valid ? s->day : -1;

    int row = 0;
    int col = first;
    for (int d = 1; d <= ndays; d++) {
        int cx = grid_x + col * cell_w;
        int cy = 19 + row * 7;
        int w = small_number_width(d);
        int nx = cx + (cell_w - w) / 2;

        if (d == today) {
            /* Highlight today with an inverse-video box. */
            ssd1306_fill_rect(dev, nx - 2, cy - 1, w + 4, 6, true);
            draw_small_number(dev, nx, cy, d, false);
        } else {
            draw_small_number(dev, nx, cy, d, true);
        }

        /* Event dot marker below the day number. */
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

static void render_mascot_select(ssd1306_t *dev, const ui_state_t *s)
{
    char buf[24];

    ssd1306_draw_string(dev, 12, 2, "SELECT MASCOT", false);

    for (int i = 0; i < CLOCK_UI_MASCOT_COUNT; i++) {
        int ox = 4 + i * 44;   /* 4, 48, 92 */
        draw_mascot(dev, ox, 18, i, s->frame);
        if (i == s->mascot_index) {
            ssd1306_draw_rect(dev, ox - 2, 16, 36, 36, true);
        }
    }

    snprintf(buf, sizeof(buf), "%s  %d/%d",
             clock_ui_mascot_name(s->mascot_index),
             s->mascot_index + 1, CLOCK_UI_MASCOT_COUNT);
    int w = (int)strlen(buf) * 8;
    ssd1306_draw_string(dev, (SSD1306_WIDTH - w) / 2, 55, buf, false);
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
    case UI_MODE_STOPWATCH:
        render_stopwatch(dev, state);
        break;
    case UI_MODE_CALENDAR:
        render_calendar(dev, state);
        break;
    case UI_MODE_MASCOT_SELECT:
        render_mascot_select(dev, state);
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
    ssd1306_draw_string(dev, 24, 8, "OLED CLOCK", false);
    draw_mascot(dev, 48, 22, mascot_index, frame);
    ssd1306_draw_string(dev, 44, 56, "V4.0", false);
    ssd1306_update(dev);
}

/* Copy `src` to `dst`, uppercasing ASCII letters and replacing anything the
 * built-in font cannot render (e.g. Thai, control bytes) with a space. */
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

void clock_ui_render_event_popup(ssd1306_t *dev, const char *title, int frame)
{
    if (dev == NULL) {
        return;
    }

    char up[40];
    to_upper_ascii(up, title ? title : "", sizeof(up));

    ssd1306_clear(dev);

    /* Flashing double border to draw attention. */
    ssd1306_draw_rect(dev, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true);
    if ((frame / 4) % 2 == 0) {
        ssd1306_draw_rect(dev, 1, 1, SSD1306_WIDTH - 2, SSD1306_HEIGHT - 2, true);
    }

    ssd1306_draw_string(dev, 16, 6, "! REMINDER !", false);
    ssd1306_draw_line(dev, 8, 18, SSD1306_WIDTH - 9, 18, true);

    /* Wrap the title over up to two centred lines of 15 characters. */
    char l1[16] = { 0 };
    char l2[16] = { 0 };
    size_t len = strlen(up);
    if (len <= 15) {
        memcpy(l1, up, len);
    } else {
        size_t brk = 15;
        for (size_t k = 15; k > 0; k--) {
            if (up[k] == ' ') {
                brk = k;
                break;
            }
        }
        memcpy(l1, up, brk);
        const char *rest = up + brk;
        while (*rest == ' ') {
            rest++;
        }
        strncpy(l2, rest, 15);
    }

    if (l1[0]) {
        ssd1306_draw_string(dev, (int)(SSD1306_WIDTH - strlen(l1) * 8) / 2, 30, l1, false);
    }
    if (l2[0]) {
        ssd1306_draw_string(dev, (int)(SSD1306_WIDTH - strlen(l2) * 8) / 2, 42, l2, false);
    }

    ssd1306_draw_string(dev, 32, 54, "MODE = OK", false);
    ssd1306_update(dev);
}
