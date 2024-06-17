#ifndef __GENERAL_H__
#define __GENERAL_H__

#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "driver/gpio.h"

#define GAIN ADS111X_GAIN_4V096 // +-4.096V
#define uS_TO_S_FACTOR 1000000
#define TIME_TO_SLEEP  300

#define BLINK_GPIO 8

typedef struct
{
	uint16_t time_year;
	uint8_t time_month;
	uint8_t time_day;
	uint8_t time_hour;
	uint8_t time_min;
	uint8_t time_sec;
} data_time;

typedef struct Data_manager
{
    int id;
    data_time time_data;
    float Soil_temp;
    float Soil_humi;
    float Env_temp;
    float Env_humi;
    uint16_t Env_lux;
} Data_t;

typedef struct Device_manager
{
    bool soil_t_status;
    bool soil_h_status;
    bool rtc_status;
} Device_t;

void lora_config();
void configure_led(void);

#endif 