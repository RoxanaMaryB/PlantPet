#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"

#include "tft.h"
#include "font.h"

// TFT ST7735 pins
#define PIN_NUM_MOSI 23
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   19
#define PIN_NUM_DC   21
#define PIN_NUM_RST  22

#define TFT_WIDTH   128
#define TFT_HEIGHT  160

#define TFT_X_OFFSET  2
#define TFT_Y_OFFSET  1

static spi_device_handle_t tft_spi;

static pet_state_t current_pet_state = PET_HAPPY;
static int anim_frame = 0;

static void tft_send_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_NUM_DC, 0);

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_transmit(tft_spi, &t);
}

static void tft_send_data(const uint8_t *data, int len)
{
    if (len == 0) return;

    gpio_set_level(PIN_NUM_DC, 1);

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_transmit(tft_spi, &t);
}

static void tft_reset(void)
{
    gpio_set_level(PIN_NUM_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_NUM_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void tft_set_addr_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t data[4];

    x0 += TFT_X_OFFSET;
    x1 += TFT_X_OFFSET;
    y0 += TFT_Y_OFFSET;
    y1 += TFT_Y_OFFSET;

    tft_send_cmd(0x2A);
    data[0] = 0x00; data[1] = x0;
    data[2] = 0x00; data[3] = x1;
    tft_send_data(data, 4);

    tft_send_cmd(0x2B);
    data[0] = 0x00; data[1] = y0;
    data[2] = 0x00; data[3] = y1;
    tft_send_data(data, 4);

    tft_send_cmd(0x2C);
}

void tft_init_display(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST),
    };
    gpio_config(&io_conf);

    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2 + 8,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_NUM_CS,
        .queue_size = 1,
    };

    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    spi_bus_add_device(SPI2_HOST, &devcfg, &tft_spi);

    tft_reset();

    tft_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_send_cmd(0x11);
    vTaskDelay(pdMS_TO_TICKS(150));

    tft_send_cmd(0x3A);
    uint8_t color_mode = 0x05;
    tft_send_data(&color_mode, 1);

    tft_send_cmd(0x36);
    uint8_t madctl = 0xC8;
    tft_send_data(&madctl, 1);

    tft_send_cmd(0x21);
    vTaskDelay(pdMS_TO_TICKS(10));

    tft_send_cmd(0x13);
    vTaskDelay(pdMS_TO_TICKS(10));

    tft_send_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void tft_fill_screen(uint16_t color)
{
    tft_set_addr_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    const int pixels = TFT_WIDTH * TFT_HEIGHT;
    const int chunk_pixels = 512;

    uint16_t *buf = heap_caps_malloc(chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!buf) return;

    for (int i = 0; i < chunk_pixels; i++) {
        buf[i] = (color >> 8) | (color << 8);
    }

    gpio_set_level(PIN_NUM_DC, 1);

    int remaining = pixels;
    while (remaining > 0) {
        int send_pixels = remaining > chunk_pixels ? chunk_pixels : remaining;

        spi_transaction_t t = {
            .length = send_pixels * 16,
            .tx_buffer = buf,
        };
        spi_device_transmit(tft_spi, &t);
        remaining -= send_pixels;
    }

    free(buf);
}

static void tft_draw_pixel(uint8_t x, uint8_t y, uint16_t color)
{
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;

    tft_set_addr_window(x, y, x, y);

    uint16_t c = (color >> 8) | (color << 8);

    gpio_set_level(PIN_NUM_DC, 1);
    spi_transaction_t t = {
        .length = 16,
        .tx_buffer = &c,
    };
    spi_device_transmit(tft_spi, &t);
}

static void tft_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            tft_draw_pixel(xx, yy, color);
        }
    }
}

static void tft_draw_hline(int x, int y, int w, uint16_t color)
{
    for (int i = 0; i < w; i++) {
        tft_draw_pixel(x + i, y, color);
    }
}

static void tft_draw_vline(int x, int y, int h, uint16_t color)
{
    for (int i = 0; i < h; i++) {
        tft_draw_pixel(x, y + i, color);
    }
}

static void tft_draw_char(uint8_t x, uint8_t y, char c, uint16_t color, uint16_t bg)
{
    if (c < 32 || c > 127) c = '?';
    const uint8_t *symb = font5x7[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t line = symb[col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                tft_draw_pixel(x + col, y + row, color);
            } else {
                tft_draw_pixel(x + col, y + row, bg);
            }
        }
    }

    for (int row = 0; row < 7; row++) {
        tft_draw_pixel(x + 5, y + row, bg);
    }
}

void tft_draw_text(uint8_t x, uint8_t y, const char *text, uint16_t color, uint16_t bg)
{
    while (*text) {
        tft_draw_char(x, y, *text, color, bg);
        x += 6;
        text++;
        if (x > TFT_WIDTH - 6) break;
    }
}

static void redraw_head(uint16_t head_color, uint16_t nose_color)
{
    // redeseneaza complet capul
    tft_fill_rect(24, 28, 80, 70, head_color);

    // nas
    tft_fill_rect(61, 58, 6, 4, nose_color);
}

static void clear_status_text(void)
{
    tft_fill_rect(18, 116, 100, 12, COLOR_BLACK);
}

void tft_draw_pet_base(void)
{
    tft_fill_screen(COLOR_BLACK);

    // urechi
    tft_fill_rect(34, 18, 14, 14, COLOR_WHITE);
    tft_fill_rect(80, 18, 14, 14, COLOR_WHITE);

    // cap
    tft_fill_rect(24, 28, 80, 70, COLOR_WHITE);

    // nas
    tft_fill_rect(61, 58, 6, 4, COLOR_RED);

    clear_status_text();
}

static void draw_happy_static(void)
{
    tft_fill_rect(42, 46, 8, 10, COLOR_BLACK);
    tft_fill_rect(78, 46, 8, 10, COLOR_BLACK);

    tft_draw_hline(53, 74, 22, COLOR_BLACK);
    tft_draw_vline(53, 68, 6, COLOR_BLACK);
    tft_draw_vline(74, 68, 6, COLOR_BLACK);

    tft_draw_text(42, 118, "HAPPY", COLOR_GREEN, COLOR_BLACK);
}

static void draw_thirsty_static(void)
{
    tft_fill_rect(42, 50, 10, 2, COLOR_BLACK);
    tft_fill_rect(76, 50, 10, 2, COLOR_BLACK);

    tft_draw_hline(55, 78, 18, COLOR_BLACK);
    tft_draw_vline(55, 78, 4, COLOR_BLACK);
    tft_draw_vline(72, 78, 4, COLOR_BLACK);

    tft_draw_text(34, 118, "THIRSTY", COLOR_CYAN, COLOR_BLACK);
}

static void draw_low_tank_static(void)
{
    // expresie ca la THIRSTY
    tft_fill_rect(42, 50, 10, 2, COLOR_BLACK);
    tft_fill_rect(76, 50, 10, 2, COLOR_BLACK);

    tft_draw_hline(55, 78, 18, COLOR_BLACK);
    tft_draw_vline(55, 78, 4, COLOR_BLACK);
    tft_draw_vline(72, 78, 4, COLOR_BLACK);

    tft_draw_text(28, 118, "LOW TANK", COLOR_RED, COLOR_BLACK);
}

static void draw_hot_static(void)
{
    tft_fill_rect(42, 48, 10, 2, COLOR_BLACK);
    tft_fill_rect(76, 48, 10, 2, COLOR_BLACK);

    tft_draw_hline(56, 76, 16, COLOR_BLACK);

    tft_draw_text(34, 118, "TOO HOT", COLOR_RED, COLOR_BLACK);
}

static void draw_dark_static(void)
{
    tft_fill_rect(40, 51, 12, 2, COLOR_BLACK);
    tft_fill_rect(76, 51, 12, 2, COLOR_BLACK);

    tft_fill_rect(60, 76, 8, 2, COLOR_BLACK);

    tft_draw_text(22, 118, "LOW LIGHT", COLOR_YELLOW, COLOR_BLACK);
}

static void draw_error_static(void)
{
    // ochi X stanga
    tft_draw_pixel(42, 46, COLOR_BLACK);
    tft_draw_pixel(43, 47, COLOR_BLACK);
    tft_draw_pixel(44, 48, COLOR_BLACK);
    tft_draw_pixel(44, 46, COLOR_BLACK);
    tft_draw_pixel(43, 47, COLOR_BLACK);
    tft_draw_pixel(42, 48, COLOR_BLACK);

    // ochi X dreapta
    tft_draw_pixel(78, 46, COLOR_BLACK);
    tft_draw_pixel(79, 47, COLOR_BLACK);
    tft_draw_pixel(80, 48, COLOR_BLACK);
    tft_draw_pixel(80, 46, COLOR_BLACK);
    tft_draw_pixel(79, 47, COLOR_BLACK);
    tft_draw_pixel(78, 48, COLOR_BLACK);

    tft_draw_hline(56, 76, 16, COLOR_BLACK);

    tft_draw_text(38, 118, "ERROR", COLOR_RED, COLOR_BLACK);
}

void tft_set_pet_state(pet_state_t state)
{
    current_pet_state = state;
    anim_frame = 0;

    if (state == PET_LOW_TANK) {
        redraw_head(COLOR_RED, COLOR_BLACK);
    } else {
        redraw_head(COLOR_WHITE, COLOR_RED);
    }
    clear_status_text();

    switch (state) {
        case PET_HAPPY:
            draw_happy_static();
            break;
        case PET_THIRSTY:
            draw_thirsty_static();
            break;
        case PET_LOW_TANK:
            draw_low_tank_static();
            break;
        case PET_HOT:
            draw_hot_static();
            break;
        case PET_DARK:
            draw_dark_static();
            break;
        case PET_ERROR:
        default:
            draw_error_static();
            break;
    }
}

void tft_pet_animate(void)
{
    anim_frame = !anim_frame;

    switch (current_pet_state) {
        case PET_HAPPY:
            // clipit: redesenam doar ochii
            tft_fill_rect(40, 44, 14, 14, COLOR_WHITE);
            tft_fill_rect(76, 44, 14, 14, COLOR_WHITE);

            if (anim_frame == 0) {
                tft_fill_rect(42, 46, 8, 10, COLOR_BLACK);
                tft_fill_rect(78, 46, 8, 10, COLOR_BLACK);
            } else {
                tft_fill_rect(40, 50, 12, 2, COLOR_BLACK);
                tft_fill_rect(76, 50, 12, 2, COLOR_BLACK);
            }
            break;

        case PET_THIRSTY:
            tft_fill_rect(87, 58, 6, 10, COLOR_WHITE);
            if (anim_frame) {
                tft_fill_rect(88, 58, 3, 6, COLOR_BLUE);
                tft_fill_rect(87, 64, 5, 3, COLOR_BLUE);
            }
            break;

        case PET_LOW_TANK:
            tft_fill_rect(87, 58, 6, 10, COLOR_RED);
            if (anim_frame) {
                tft_fill_rect(88, 58, 3, 6, COLOR_BLUE);
                tft_fill_rect(87, 64, 5, 3, COLOR_BLUE);
            }
            break;
        case PET_HOT:
            // picatura transpiratie apare/dispare
            tft_fill_rect(89, 40, 6, 10, COLOR_WHITE);
            if (anim_frame) {
                tft_fill_rect(90, 40, 3, 7, COLOR_CYAN);
                tft_fill_rect(89, 47, 5, 2, COLOR_CYAN);
            }
            break;

        case PET_DARK:
            // Z apare/dispare
            tft_fill_rect(92, 36, 8, 8, COLOR_WHITE);
            if (anim_frame) {
                tft_draw_text(92, 36, "Z", COLOR_BLUE, COLOR_WHITE);
            }
            break;

        case PET_ERROR:
        default:
            // fara animatie
            break;
    }
}