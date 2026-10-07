#ifndef CLOCK_UI_H
#define CLOCK_UI_H

#include "ssd1306.h"

// รายการโหมดหน้าจอทั้งหมด
typedef enum {
    UI_MODE_CLOCK = 0,
    UI_MODE_STOPWATCH,
    UI_MODE_MASCOT_SELECT,
    UI_MODE_MAX
} ui_mode_t;

void clock_ui_init_orientation(void);

// ฟังก์ชัน Render หลักที่รองรับการเปลี่ยนโหมด
void clock_ui_render_mode(ssd1306_t *dev, ui_mode_t mode, int hours, int minutes, int seconds, 
                         int day, int month, int year, const char *status, int frame, 
                         uint32_t sw_ms, int selected_mascot);

#endif // CLOCK_UI_H