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

static spi_device_handle_t tft_spi;

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

void tft_draw_frame(void)
{
    tft_fill_screen(COLOR_BLACK);

    tft_draw_text(5, 5,  "PLANTPET", COLOR_YELLOW, COLOR_BLACK);
    tft_draw_text(5, 20, "TEMP:", COLOR_WHITE, COLOR_BLACK);
    tft_draw_text(5, 35, "HUM:", COLOR_WHITE, COLOR_BLACK);
    tft_draw_text(5, 50, "SOIL:", COLOR_WHITE, COLOR_BLACK);
    tft_draw_text(5, 65, "LIGHT:", COLOR_WHITE, COLOR_BLACK);
    tft_draw_text(5, 80, "TANK:", COLOR_WHITE, COLOR_BLACK);
    tft_draw_text(5, 95, "LAMP:", COLOR_WHITE, COLOR_BLACK);
    tft_draw_text(5, 110, "PUMP:", COLOR_WHITE, COLOR_BLACK);
    tft_draw_text(5, 135, "STATE:", COLOR_CYAN, COLOR_BLACK);
}

void tft_draw_value_line(uint8_t y, const char *value, uint16_t color)
{
    tft_draw_text(55, y, "            ", COLOR_BLACK, COLOR_BLACK);
    tft_draw_text(55, y, value, color, COLOR_BLACK);
}