#ifndef TFT_H
#define TFT_H

#include <stdint.h>
#include <stdbool.h>

#define COLOR_BLACK   0xFFFF
#define COLOR_WHITE   0x0000
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410

void tft_init_display(void);
void tft_fill_screen(uint16_t color);
void tft_draw_text(uint8_t x, uint8_t y, const char *text, uint16_t color, uint16_t bg);
void tft_draw_frame(void);
void tft_draw_value_line(uint8_t y, const char *value, uint16_t color);

#endif