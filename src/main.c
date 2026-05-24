#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "webserver.h"
#include "esp_timer.h"
#include "tft.h"


// PINI

// Senzori
#define FLOAT_GPIO      GPIO_NUM_13
#define DHT_GPIO        GPIO_NUM_27
#define LIGHT_CTRL_GPIO GPIO_NUM_25   // bec prin modul MOSFET
#define PUMP_CTRL_GPIO  GPIO_NUM_26   // pompa prin modul MOSFET

// Pompa
#define PUMP_WATER_TIME_MS   5000
#define PUMP_COOLDOWN_MS    15000

// ADC
#define SOIL_PIN        ADC_CHANNEL_6   // GPIO34
#define LDR_PIN         ADC_CHANNEL_7   // GPIO35

thresholds_t g_thresholds = {
    .ldr_threshold = 2000,
    .soil_threshold = 2000,
    .temp_threshold = 26,
};

sensor_state_t g_state = {0};

static int64_t g_last_valid_dht_us = 0;

static int prev_dht_show_error = -1;
static int prev_low_tank = -1;
static int prev_too_hot = -1;
static int prev_needs_water = -1;
static int prev_low_light = -1;
static int prev_lamp_on = -1;
static int prev_pump_on = -1;

static int pump_on = 0;
static int64_t pump_started_at_us = 0;
static int64_t pump_last_cycle_end_us = 0;

// ADC

static adc_oneshot_unit_handle_t adc1_handle;

// DHT11

static int dht11_read(int *temperature, int *humidity)
{
    uint8_t data[5] = {0, 0, 0, 0, 0};

    gpio_set_direction(DHT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT_GPIO, 1);
    esp_rom_delay_us(30);

    gpio_set_direction(DHT_GPIO, GPIO_MODE_INPUT);

    uint32_t timeout = 0;

    while (gpio_get_level(DHT_GPIO) == 1) {
        esp_rom_delay_us(1);
        if (++timeout > 100) return -1; // nu a tras linia in low dupa start
    }

    timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 0) {
        esp_rom_delay_us(1);
        if (++timeout > 100) return -2; // nu a tras linia in high dupa start
    }

    timeout = 0;
    while (gpio_get_level(DHT_GPIO) == 1) {
        esp_rom_delay_us(1);
        if (++timeout > 100) return -3; // nu a tras linia in low dupa data
    }

    for (int i = 0; i < 40; i++) {
        timeout = 0;
        while (gpio_get_level(DHT_GPIO) == 0) {
            esp_rom_delay_us(1);
            if (++timeout > 100) return -4; // nu a tras linia in high la inceputul bitului
        }

        uint32_t width = 0;
        while (gpio_get_level(DHT_GPIO) == 1) {
            esp_rom_delay_us(1);
            if (++width > 100) return -5; // nu a tras linia in low la sfarsitul bitului
        }

        data[i / 8] <<= 1;
        if (width > 40) {
            data[i / 8] |= 1;
        }
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        return -6; // checksum invalid, date corupte
    }

    *humidity = data[0];
    *temperature = data[2];
    return 0;
}

// MAIN

void app_main(void)
{

    g_last_valid_dht_us = esp_timer_get_time();
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_softap();
    start_webserver();

    printf("Pornire demo senzori + TFT + control lumina\n");

    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LIGHT_CTRL_GPIO) |
                        (1ULL << PUMP_CTRL_GPIO),
    };

    gpio_config(&io_conf);

    gpio_set_level(LIGHT_CTRL_GPIO, 0); // bec stins la pornire
    gpio_set_level(PUMP_CTRL_GPIO, 0);  // pompa oprita la pornire

    gpio_config_t float_conf = {
        .pin_bit_mask = (1ULL << FLOAT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&float_conf);

    tft_init_display();
    tft_draw_frame();

    // ADC
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_config1, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    adc_oneshot_config_channel(adc1_handle, SOIL_PIN, &config);
    adc_oneshot_config_channel(adc1_handle, LDR_PIN, &config);

    char buf[32];

    while (1) {
        int soil_raw = 0;
        int ldr_raw = 0;
        int float_state = 0;
        int temp = -1;
        int hum = -1;

        adc_oneshot_read(adc1_handle, SOIL_PIN, &soil_raw);
        adc_oneshot_read(adc1_handle, LDR_PIN, &ldr_raw);

        float_state = gpio_get_level(FLOAT_GPIO);

        int dht_status = dht11_read(&temp, &hum);

        if (dht_status == 0) {
            g_last_valid_dht_us = esp_timer_get_time();
        }

        bool dht_show_error = false;
        int64_t now_us_dht = esp_timer_get_time();

        if (dht_status != 0 && (now_us_dht - g_last_valid_dht_us) > 40000000LL) { // 40 secunde fara date => ERR
            dht_show_error = true;
        }

        bool low_tank = (float_state == 0);
        bool needs_water = (soil_raw > g_thresholds.soil_threshold);

        // control bec
        static int lamp_on = 0;
        int ldr_on_threshold = g_thresholds.ldr_threshold + 100;
        int ldr_off_threshold = g_thresholds.ldr_threshold - 100;

        if (!lamp_on && ldr_raw > ldr_on_threshold) {
            lamp_on = 1;
        } else if (lamp_on && ldr_raw < ldr_off_threshold) {
            lamp_on = 0;
        }

        gpio_set_level(LIGHT_CTRL_GPIO, lamp_on);

        bool low_light = (lamp_on != 0);

        // control pompa
        int64_t now_us_pompa = esp_timer_get_time();

        bool tank_ok = (float_state != 0);
        bool soil_needs_water = (soil_raw > g_thresholds.soil_threshold);

        if (pump_on) {
            // daca pompa merge deja, o oprim dupa durata setata
            if ((now_us_pompa - pump_started_at_us) >= (PUMP_WATER_TIME_MS * 1000LL)) {
                gpio_set_level(PUMP_CTRL_GPIO, 0);
                pump_on = 0;
                pump_last_cycle_end_us = now_us_pompa;
            }
        } else {
            // pompa porneste daca:
            // rezervorul are apa
            // solul e uscat
            // a trecut cooldown-ul de la ultima udare
            if (tank_ok &&
                soil_needs_water &&
                ((now_us_pompa - pump_last_cycle_end_us) >= (PUMP_COOLDOWN_MS * 1000LL))) {

                gpio_set_level(PUMP_CTRL_GPIO, 1);
                pump_on = 1;
                pump_started_at_us = now_us_pompa;
            } else {
                gpio_set_level(PUMP_CTRL_GPIO, 0);
                pump_on = 0;
            }
        }

        // serial monitor
        printf("--------------------------------------------------\n");
        if (dht_status == 0) {
            printf("Temp : %.2f C | Hum: %d %%\n", (float)temp, hum);
            printf("Temp status: %s\n", (temp >= g_thresholds.temp_threshold) ? "TOO HOT" : "OK");
        } else {
            printf("DHT11 eroare: %d\n", dht_status);
        }

        printf("Soil raw   : %d | %s\n", soil_raw, (soil_raw > g_thresholds.soil_threshold) ? "DRY / NEEDS WATER" : "OK / HAS WATER");
        printf("LDR raw    : %d\n", ldr_raw);
        printf("Tank       : %s\n", float_state == 0 ? "LOW / trigger" : "OK");
        printf("Lamp       : %s\n", lamp_on ? "ON" : "OFF");
        printf("Pump       : %s\n", pump_on ? "ON" : "OFF");

        g_state.soil_raw = soil_raw;
        g_state.ldr_raw = ldr_raw;
        g_state.float_state = float_state;
        if (dht_status == 0) {
            g_state.temp = temp;
            g_state.hum = hum;
        }
        g_state.dht_status = dht_status;
        g_state.lamp_on = lamp_on;
        g_state.pump_on = pump_on;

        // TFT update
        bool too_hot = (g_state.temp >= g_thresholds.temp_threshold);

        bool tft_needs_update = false;

        if (prev_dht_show_error != (int)dht_show_error) tft_needs_update = true;
        if (prev_low_tank != (int)low_tank) tft_needs_update = true;
        if (prev_too_hot != (int)too_hot) tft_needs_update = true;
        if (prev_needs_water != (int)needs_water) tft_needs_update = true;
        if (prev_low_light != (int)low_light) tft_needs_update = true;
        if (prev_lamp_on != lamp_on) tft_needs_update = true;
        if (prev_pump_on != pump_on) tft_needs_update = true;

        if (tft_needs_update) {
            if (dht_show_error) {
                tft_draw_value_line(20, "ERR", COLOR_RED);
                tft_draw_value_line(35, "ERR", COLOR_RED);
            } else {
                snprintf(buf, sizeof(buf), "%.2f C", (float)g_state.temp);
                tft_draw_value_line(20, buf,
                                    (g_state.temp >= g_thresholds.temp_threshold) ? COLOR_RED : COLOR_GREEN);

                snprintf(buf, sizeof(buf), "%d %%", g_state.hum);
                tft_draw_value_line(35, buf, COLOR_GREEN);
            }

            snprintf(buf, sizeof(buf), "%d %s",
                    g_state.soil_raw,
                    (g_state.soil_raw > g_thresholds.soil_threshold) ? "DRY" : "OK");
            tft_draw_value_line(50, buf,
                                (g_state.soil_raw > g_thresholds.soil_threshold) ? COLOR_RED : COLOR_GREEN);

            snprintf(buf, sizeof(buf), "%d", g_state.ldr_raw);
            tft_draw_value_line(65, buf, COLOR_WHITE);

            tft_draw_value_line(80, low_tank ? "LOW" : "OK",
                                low_tank ? COLOR_RED : COLOR_GREEN);

            tft_draw_value_line(95, lamp_on ? "ON" : "OFF",
                                lamp_on ? COLOR_YELLOW : COLOR_GRAY);

            tft_draw_value_line(110, pump_on ? "ON" : "OFF",
                                pump_on ? COLOR_CYAN : COLOR_GRAY);

            if (dht_show_error) {
                tft_draw_value_line(135, "DHT ERROR", COLOR_RED);
            } else if (low_tank) {
                tft_draw_value_line(135, "LOW TANK", COLOR_RED);
            } else if (too_hot) {
                tft_draw_value_line(135, "TOO HOT", COLOR_RED);
            } else if (needs_water) {
                tft_draw_value_line(135, "NEEDS WATER", COLOR_RED);
            } else if (low_light) {
                tft_draw_value_line(135, "LOW LIGHT", COLOR_YELLOW);
            } else if (pump_on) {
                tft_draw_value_line(135, "PUMPING", COLOR_CYAN);
            } else {
                tft_draw_value_line(135, "HAPPY", COLOR_GREEN);
            }

            prev_dht_show_error = (int)dht_show_error;
            prev_low_tank = (int)low_tank;
            prev_too_hot = (int)too_hot;
            prev_needs_water = (int)needs_water;
            prev_low_light = (int)low_light;
            prev_lamp_on = lamp_on;
            prev_pump_on = pump_on;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}