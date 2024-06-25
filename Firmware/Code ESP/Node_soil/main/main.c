#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "sdkconfig.h"

#include <esp_log.h>
#include <esp_err.h>
#include "esp_sleep.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include <freertos/semphr.h>

#include "driver/gpio.h"

#include "lora.h"
#include "general.h"
#include <ds3231.h>
#include <ads111x.h>
#include <ds18x20.h>


#define GAIN ADS111X_GAIN_4V096 // +-4.096V

TaskHandle_t send_lora_handle = NULL;
TaskHandle_t rtc_handle = NULL;
TaskHandle_t Soil_temp_handle = NULL;
TaskHandle_t Soil_humi_handle = NULL;

SemaphoreHandle_t I2C_mutex = NULL;

static EventGroupHandle_t lora_event_group;
const int TASK1_DONE_BIT = BIT0;
const int TASK2_DONE_BIT = BIT1;
static 	i2c_dev_t dev;
static i2c_dev_t devices;
static float gain_val;

static const gpio_num_t SENSOR_GPIO = CONFIG_EXAMPLE_ONEWIRE_GPIO;
static const onewire_addr_t SENSOR_ADDR = CONFIG_EXAMPLE_DS18X20_ADDR;

int64_t start_time, end_time, elapsed_time;

static const char *TAG = "Env_node";

volatile Data_t Data;

void send_lora_task(void *pvParameters)
{
	gpio_set_level(BLINK_GPIO, 1);
	EventBits_t uxBits = xEventGroupWaitBits(
        lora_event_group,                // The event group being tested
        TASK1_DONE_BIT | TASK2_DONE_BIT, // The bits within the event group to wait for
        pdTRUE,                     // Clear the bits before returning
        pdTRUE,                     // Wait for both bits to be set
        portMAX_DELAY);
	uint8_t buf[256];
	while(1) {
		if((uxBits & (TASK1_DONE_BIT | TASK2_DONE_BIT)) == (TASK1_DONE_BIT | TASK2_DONE_BIT)){
			Data_t lora_data = Data;

			int send_len = sprintf((char *)buf,"%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x",
				lora_data.id,
				*(unsigned int *)&lora_data.Soil_temp,
				*(unsigned int *)&lora_data.Soil_humi,
				*(unsigned int *)&lora_data.Env_temp,
				*(unsigned int *)&lora_data.Env_humi,
				*(unsigned int *)&lora_data.Env_lux,
				*(unsigned int *)&lora_data.time_data.time_year,
				*(unsigned int *)&lora_data.time_data.time_month, 
				*(unsigned int *)&lora_data.time_data.time_day, 
				*(unsigned int *)&lora_data.time_data.time_hour, 
				*(unsigned int *)&lora_data.time_data.time_min, 
				*(unsigned int *)&lora_data.time_data.time_sec
				// *(unsigned int *)&check_data
				);

			lora_send_packet(buf, send_len);
			ESP_LOGI(pcTaskGetName(NULL), "%d byte packet sent: %s", send_len, buf);
			int lost = lora_packet_lost();
			if (lost != 0) {
				ESP_LOGW(pcTaskGetName(NULL), "%d packets lost", lost);
			}
			esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
			end_time = esp_timer_get_time();
			elapsed_time = end_time - start_time;
			printf("Elapsed time: %lld ms\n", elapsed_time/1000);
			esp_deep_sleep_start();
			// vTaskDelay(pdMS_TO_TICKS(10000));
		}
	}
}

void ads111x_task(void *pvParameters)
{
	gain_val = ads111x_gain_values[GAIN];

	ESP_ERROR_CHECK(ads111x_init_desc(&devices, ADS111X_ADDR_GND, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));


    
	while(1)
	{
	if(xSemaphoreTake(I2C_mutex, portMAX_DELAY) == pdTRUE)
	{
		ESP_LOGI(__func__, "ADS");
		ESP_ERROR_CHECK(ads111x_set_mode(&devices, ADS111X_MODE_CONTINUOUS));    // Continuous conversion mo	de
		ESP_ERROR_CHECK(ads111x_set_data_rate(&devices, ADS111X_DATA_RATE_32)); // 32 samples per second
		ESP_ERROR_CHECK(ads111x_set_input_mux(&devices, ADS111X_MUX_0_GND));    // positive = AIN0, negative = GND
		ESP_ERROR_CHECK(ads111x_set_gain(&devices, GAIN));		
		int16_t raw = 0;
		if (ads111x_get_value(&devices, &raw) == ESP_OK){
			float voltage = gain_val / ADS111X_MAX_VALUE * raw;
			Data.Soil_humi = voltage;
			printf(" Raw ADC value: %d, voltage: %.04f volts\n", raw, Data.Soil_humi);
		} else {
			printf(" Cannot read ADC value\n");
		}
		
		xSemaphoreGive(I2C_mutex);
	}
	xEventGroupSetBits(lora_event_group, TASK1_DONE_BIT);
	vTaskDelay(pdMS_TO_TICKS(5000));
	}
}

void ds18b20_task(void *pvParameter)
{
    gpio_set_pull_mode(SENSOR_GPIO, GPIO_PULLUP_ONLY);

    float temperature;
    while (1)
    {
        if (ds18x20_measure_and_read(SENSOR_GPIO, SENSOR_ADDR, &temperature) != ESP_OK)
            ESP_LOGE(__func__, "Could not read from sensor");
        else
            ESP_LOGI(__func__, "Sensor %08" PRIx32 "%08" PRIx32 ": %.2f°C",
                    (uint32_t)(SENSOR_ADDR >> 32), (uint32_t)SENSOR_ADDR, temperature);

		Data.Soil_temp = temperature;

		xEventGroupSetBits(lora_event_group, TASK2_DONE_BIT);
		vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void rtc_task(void *pvParameters)
{

    // memset(&dev, 0, sizeof(i2c_dev_t));

    ESP_ERROR_CHECK(ds3231_init_desc(&dev, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));  	

	struct tm time = {};
	while(1){
		if(xSemaphoreTake(I2C_mutex, portMAX_DELAY) == pdTRUE)
		{
			if (ds3231_get_time(&dev, &time) != ESP_OK)
			{
				printf("Could not get time\n");
				continue;
			}

			Data.time_data.time_year = time.tm_year + 1900;
			Data.time_data.time_month = time.tm_mon + 1;
			Data.time_data.time_day = time.tm_mday;
			Data.time_data.time_hour = time.tm_hour;
			Data.time_data.time_min = time.tm_min;
			Data.time_data.time_sec = time.tm_sec;

			printf("%02d/%02d/%04d %02d:%02d:%02d\n", 
				Data.time_data.time_day, 
				Data.time_data.time_month,
				Data.time_data.time_year, 
				Data.time_data.time_hour, 
				Data.time_data.time_min, 
				Data.time_data.time_sec
			);
			xSemaphoreGive(I2C_mutex);
		}

		vTaskDelay(pdMS_TO_TICKS(1000));
	}
	vTaskDelete(NULL);

}

void app_main(void)
{
	start_time = esp_timer_get_time();

	// configure_led();
	// gpio_set_level(BLINK_GPIO, 1);
	// vTaskDelay(pdMS_TO_TICKS(10000));
	// gpio_set_level(BLINK_GPIO, 0);
	
	Data.id = NODE_ID;
    if (lora_init() == 0) {
		ESP_LOGE(pcTaskGetName(NULL), "Does not recognize the module");
	} else {
		ESP_LOGI(pcTaskGetName(NULL), "Recognize the module");
		lora_config();
	}

	

	ESP_ERROR_CHECK(i2cdev_init());
	I2C_mutex = xSemaphoreCreateMutex();
	lora_event_group = xEventGroupCreate();

	xTaskCreatePinnedToCore(rtc_task, "rtc_task", 2048*2, NULL, 4, &rtc_handle, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(ads111x_task, "ads111x_task", 2048*2, NULL, 5, &Soil_humi_handle, tskNO_AFFINITY);	
	xTaskCreatePinnedToCore(ds18b20_task, "ds18b20_task", 2048*2, NULL, 5, &Soil_temp_handle, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(send_lora_task, "send_lora_task", 2048*2, NULL, 6, &send_lora_handle, tskNO_AFFINITY);
}

void lora_config(){
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is 433MHz");
	lora_set_frequency(433e6); // 433MHz
	lora_enable_crc();

	int cr, bw, sf;
    #if CONFIF_ADVANCED
        cr = CONFIG_CODING_RATE`
        bw = CONFIG_BANDWIDTH;
        sf = CONFIG_SF_RATE;
    #endif

	lora_set_coding_rate(CONFIG_CODING_RATE);
	lora_set_bandwidth(CONFIG_BANDWIDTH);
	lora_set_spreading_factor(CONFIG_SF_RATE);

	bw = lora_get_bandwidth();
	cr = lora_get_coding_rate();
	sf = lora_get_spreading_factor();
	ESP_LOGI(pcTaskGetName(NULL), "coding_rate=%d | bandwidth=%d | spreading_factor=%d", cr, bw, sf);
}

void configure_led(void)
{
    ESP_LOGI(TAG, "Example configured to blink GPIO LED!");
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

// #include <stdio.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <ds3231.h>
// #include <string.h>

// void ds3231_test(void *pvParameters)
// {
//     i2c_dev_t dev;
//     memset(&dev, 0, sizeof(i2c_dev_t));

//     ESP_ERROR_CHECK(ds3231_init_desc(&dev, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));

//     // setup datetime: 2016-10-09 13:50:10
//     struct tm time = {
//         .tm_year = 124, //since 1900 (2016 - 1900)
//         .tm_mon  = 5,  // 0-based
//         .tm_mday = 13,
//         .tm_hour = 00,
//         .tm_min  = 17,
//         .tm_sec  = 0
//     };
//     ESP_ERROR_CHECK(ds3231_set_time(&dev, &time));

//     while (1)
//     {
//         float temp;

//         vTaskDelay(pdMS_TO_TICKS(250));

//         if (ds3231_get_temp_float(&dev, &temp) != ESP_OK)
//         {
//             printf("Could not get temperature\n");
//             continue;
//         }

//         if (ds3231_get_time(&dev, &time) != ESP_OK)
//         {
//             printf("Could not get time\n");
//             continue;
//         }

//         /* float is used in printf(). you need non-default configuration in
//          * sdkconfig for ESP8266, which is enabled by default for this
//          * example. see sdkconfig.defaults.esp8266
//          */
//         printf("%04d-%02d-%02d %02d:%02d:%02d, %.2f deg Cel\n", time.tm_year + 1900 /*Add 1900 for better readability*/, time.tm_mon + 1,
//             time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, temp);
//     }
// }

// void app_main()
// {
//     ESP_ERROR_CHECK(i2cdev_init());
//     xTaskCreate(ds3231_test, "ds3231_test", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
// }