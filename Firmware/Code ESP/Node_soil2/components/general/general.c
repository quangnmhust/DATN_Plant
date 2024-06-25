#include <stdio.h>
#include "general.h"

float calculate_checksum(Data_t *data){
    float checksum = 0;
    checksum =  (float)data->time_data.time_day + 
                (float)data->time_data.time_month + 
                (float)data->time_data.time_year + 
                (float)data->time_data.time_hour + 
                (float)data->time_data.time_min + 
                (float)data->time_data.time_sec + 
                data->Soil_humi + 
                data->Soil_temp +
                data->Env_humi +
                data->Env_temp +
                (float)data->Env_lux+
                (float)data->id;
    return checksum;
}
