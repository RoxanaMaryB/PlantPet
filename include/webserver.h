#ifndef WEBSERVER_H
#define WEBSERVER_H

typedef struct {
    int ldr_threshold;
    int soil_threshold;
    int temp_threshold;
} thresholds_t;

typedef struct {
    int soil_raw;
    int ldr_raw;
    int float_state;
    int temp;
    int hum;
    int dht_status;
    int lamp_on;
    int pump_on;
} sensor_state_t;

extern thresholds_t g_thresholds;
extern sensor_state_t g_state;

void wifi_init_softap(void);
void start_webserver(void);

#endif