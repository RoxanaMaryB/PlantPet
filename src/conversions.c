#include "conversions.h"

int clamp_int(int v, int min_v, int max_v)
{
    if (v < min_v) return min_v;
    if (v > max_v) return max_v;
    return v;
}

int soil_raw_to_percent(int soil_raw)
{
    const int SOIL_DRY_RAW = 2941;
    const int SOIL_WET_RAW = 1023;

    int percent = 100 * (SOIL_DRY_RAW - soil_raw) / (SOIL_DRY_RAW - SOIL_WET_RAW);
    return clamp_int(percent, 0, 100);
}

int ldr_raw_to_percent(int ldr_raw)
{
    const int LDR_BRIGHT_RAW = 0;
    const int LDR_DARK_RAW   = 4000;

    int percent = 100 * (LDR_DARK_RAW - ldr_raw) / (LDR_DARK_RAW - LDR_BRIGHT_RAW);
    return clamp_int(percent, 0, 100);
}

const char *soil_percent_to_label(int percent)
{
    if (percent <= 25) return "DRY";
    if (percent <= 70) return "OK";
    return "WET";
}

const char *light_percent_to_label(int percent)
{
    if (percent <= 20) return "DARK";
    if (percent <= 60) return "MEDIUM";
    return "BRIGHT";
}