#ifndef CONVERSIONS_H
#define CONVERSIONS_H

int clamp_int(int v, int min_v, int max_v);

int soil_raw_to_percent(int soil_raw);
int ldr_raw_to_percent(int ldr_raw);

const char *soil_percent_to_label(int percent);
const char *light_percent_to_label(int percent);

#endif