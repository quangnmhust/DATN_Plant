#include <stdio.h>
#include "datamanager.h"

void decodeHexToData(const char *datauart, Data_t *data)
{
    sscanf(datauart, "%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
    (unsigned int *) &data->id,
    (unsigned int *)&data->Soil_temp, 
    (unsigned int *)&data->Soil_humi, 
    (unsigned int *)&data->Env_temp, 
    (unsigned int *)&data->Env_humi, 
    (unsigned int *)&data->Env_lux, 
    (unsigned int *)&data->time_data.time_year, 
    (unsigned int *)&data->time_data.time_month, 
    (unsigned int *)&data->time_data.time_day, 
    (unsigned int *)&data->time_data.time_hour, 
    (unsigned int *)&data->time_data.time_min, 
    (unsigned int *)&data->time_data.time_sec);
}
