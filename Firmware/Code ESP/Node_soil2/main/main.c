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
// #include "esp32/ulp.h"
#include "esp_sleep.h"
#include <esp_system.h>
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include <freertos/semphr.h>

#include "driver/gpio.h"

#include "lora.h"
#include "general.h"
#include <ds3231.h>
#include <bh1750.h>
#include <sht4x.h>

static const char *TAG = "Env_node";

TaskHandle_t send_lora_handle = NULL;
TaskHandle_t rtc_handle = NULL;
TaskHandle_t Env_sensor_handle = NULL;
TaskHandle_t Env_lux_handle = NULL;

SemaphoreHandle_t I2C_mutex = NULL;

static EventGroupHandle_t lora_event_group;
const int TASK1_DONE_BIT = BIT0;
const int TASK2_DONE_BIT = BIT1;

int64_t start_time, end_time;

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
			// vTaskDelay(pdMS_TO_TICKS(6000));
			esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
			end_time = esp_timer_get_time();
			int64_t elapsed_time = end_time - start_time;
			// In thời gian thực hiện (đơn vị milli giây)
			printf("Elapsed time: %lld ms\n", elapsed_time/1000);
			esp_deep_sleep_start();
			// vTaskDelete(NULL);
			// vTaskDelay(pdMS_TO_TICKS(10000));
		}
	}
}

void bh1750_task(void *pvParameters)
{
	uint16_t lux;

	i2c_dev_t bh1750;
	memset(&bh1750, 0, sizeof(i2c_dev_t)); // Zero descriptor
    ESP_ERROR_CHECK(bh1750_init_desc(&bh1750, BH1750_ADDR_LO, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(bh1750_setup(&bh1750, BH1750_MODE_CONTINUOUS, BH1750_RES_HIGH));

    while (1)
    {
		if(xSemaphoreTake(I2C_mutex, portMAX_DELAY) == pdTRUE){
			if (bh1750_read(&bh1750, &lux) != ESP_OK){
				printf("Could not read lux data\n");
			} else {
				Data.Env_lux = lux;
				printf("Lux: %d\n", Data.Env_lux);
			}
			xSemaphoreGive(I2C_mutex);
		}
		xEventGroupSetBits(lora_event_group, TASK1_DONE_BIT);
		vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void sht40_task(void *pvParameter)
{
	float temperature;
    float humidity;

	sht4x_t sht40;
	memset(&sht40, 0, sizeof(sht4x_t));
    ESP_ERROR_CHECK(sht4x_init_desc(&sht40, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(sht4x_init(&sht40));
	
    while (1)
    {
		if(xSemaphoreTake(I2C_mutex, portMAX_DELAY) == pdTRUE){
			ESP_ERROR_CHECK(sht4x_measure(&sht40, &temperature, &humidity));
			Data.Env_temp = temperature;
			Data.Env_humi = humidity;
        	printf("sht4x Sensor: %.2f °C, %.2f %%\n", Data.Env_temp, Data.Env_humi);
			xSemaphoreGive(I2C_mutex);
		}
		xEventGroupSetBits(lora_event_group, TASK2_DONE_BIT);
		vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void rtc_task(void *pvParameters)
{
	struct tm time = {};

	i2c_dev_t dev;
	memset(&dev, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(ds3231_init_desc(&dev, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));

	while(1){
		if(xSemaphoreTake(I2C_mutex, portMAX_DELAY) == pdTRUE){
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

			printf("%02d-%02d-%04d %02d:%02d:%02d\n", 
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
}

void app_main(void)
{	
	start_time = esp_timer_get_time();
	Data.id = NODE_ID;
    if (lora_init() == 0) {
		ESP_LOGE(pcTaskGetName(NULL), "Does not recognize the module");
	} else {
		lora_config();
	}

	// configure_led();

	ESP_ERROR_CHECK(i2cdev_init());
	I2C_mutex = xSemaphoreCreateMutex();
	lora_event_group = xEventGroupCreate();

	xTaskCreatePinnedToCore(rtc_task, "rtc_task", 2048*2, NULL, 4, &rtc_handle, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(bh1750_task, "bh1750_task", 2048*2, NULL, 5, &Env_lux_handle, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(sht40_task, "sht40_task", 2048*2, NULL, 5, &Env_sensor_handle, tskNO_AFFINITY);
	xTaskCreatePinnedToCore(send_lora_task, "send_lora_task", 2048*2, NULL, 6, &send_lora_handle, tskNO_AFFINITY);
}

void lora_config(){
	ESP_LOGI(pcTaskGetName(NULL), "Frequency is 433MHz");
	lora_set_frequency(433e6); // 433MHz
	lora_enable_crc();

	int cr, bw, sf;
    #if CONFIF_ADVANCED
        cr = CONFIG_CODING_RATE
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
    /* Set the GPIO as a push/pull output */
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
//         .tm_mday = 26,
//         .tm_hour = 06,
//         .tm_min  = 24,
//         .tm_sec  = 10
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

