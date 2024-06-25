#ifndef __DATAMANAGER_H__
#define __DATAMANAGER_H__

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"
#include "freertos/semphr.h"


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
    float snr_lora;
    int rssi_lora;
    int time_lora;
} Data_t;

void decodeHexToData(const char *datauart, Data_t *data);

extern Data_t data_mqtt;
#endif