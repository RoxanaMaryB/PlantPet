#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "webserver.h"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    const size_t html_size = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start, html_size);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    char resp[256];

    snprintf(resp, sizeof(resp),
        "{"
        "\"temp\":\"%.2f C\","
        "\"hum\":\"%d %%\","
        "\"soil_raw\":%d,"
        "\"ldr_raw\":%d,"
        "\"float_state\":%d,"
        "\"lamp_on\":%d,"
        "\"ldr_threshold\":%d,"
        "\"soil_threshold\":%d,"
        "\"temp_threshold\":%d,"
        "\"pump_on\":%d"
        "}",
        (float)g_state.temp,
        g_state.hum,
        g_state.soil_raw,
        g_state.ldr_raw,
        g_state.float_state,
        g_state.lamp_on,
        g_thresholds.ldr_threshold,
        g_thresholds.soil_threshold,
        g_thresholds.temp_threshold,
        g_state.pump_on
    );

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t set_get_handler(httpd_req_t *req)
{
    char query[128];
    char param[16];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        if (httpd_query_key_value(query, "ldr", param, sizeof(param)) == ESP_OK) {
            g_thresholds.ldr_threshold = atoi(param);
        }
        if (httpd_query_key_value(query, "soil", param, sizeof(param)) == ESP_OK) {
            g_thresholds.soil_threshold = atoi(param);
        }
        if (httpd_query_key_value(query, "temp", param, sizeof(param)) == ESP_OK) {
            g_thresholds.temp_threshold = atoi(param);
        }
    }

    return httpd_resp_sendstr(req, "Thresholds updated");
}

void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t status = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status);

        httpd_uri_t set = {
            .uri = "/set",
            .method = HTTP_GET,
            .handler = set_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &set);

        printf("Webserver pornit.\n");
    }
}

void wifi_init_softap(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "PlantPet",
            .ssid_len = strlen("PlantPet"),
            .password = "plantpet123",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK
        },
    };

    if (strlen("plantpet123") == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();

    printf("WiFi AP pornit.\n");
    printf("SSID: PlantPet\n");
    printf("Parola: plantpet123\n");
    printf("Deschide in browser: http://192.168.4.1\n");
}