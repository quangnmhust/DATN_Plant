/* The example of ESP-IDF
 *
 * This sample code is in the public domain.
 */

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "mqtt.h"
#include "lora.h"
#include "datamanager.h"
#include "freertos/event_groups.h"

Data_t data_mqtt;

#define BIT_0 (1 << 0)

void send_data_task_mqtt(void *arg);
void uart_rx_task_mqtt(void *arg);
void Timeout_task_mqtt(void *arg);
static EventGroupHandle_t xEventGroup;

char *TAG = "MAIN";
void ManageDatafromUarttoSIM(void *arg)
{
	if(xTaskCreatePinnedToCore(send_data_task_mqtt, "send data uart task", 1024*8, NULL, 32, NULL, tskNO_AFFINITY) == pdPASS)
	{
		xTaskCreatePinnedToCore(uart_rx_task_mqtt, "uart rx task", 1024*6, NULL, 1, NULL, tskNO_AFFINITY);
	    xTaskCreatePinnedToCore(Timeout_task_mqtt, "timeout task",3072, NULL, 3, NULL, tskNO_AFFINITY);
	}
	else
	{
		ESP_LOGI(pcTaskGetName(NULL), "Task send data uart task could not be created");
	}
    vTaskDelete(NULL);
}

void TaskSIM(void *arg)
{
	ESP_LOGI(pcTaskGetName(NULL), " Task SIM waiting for BIT_0");
	EventBits_t uxBits = xEventGroupWaitBits(
		xEventGroup, 
		BIT_0,        
		pdTRUE,       
		pdFALSE,      
		portMAX_DELAY  
	);
	if((uxBits & BIT_0) == BIT_0)
	{
		ESP_LOGI(pcTaskGetName(NULL), " Task SIM received BIT_0");
		ManageDatafromUarttoSIM(NULL);
	}
	vTaskDelete(NULL);
}


void task_rx(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	uint8_t buf[256]; // Maximum Payload size of SX1276/77/78/79 is 255
	Data_t data_receive;
	while(1) {
		lora_receive(); // put into receive mode
		if (lora_received()) 
		{
			int rxLen = lora_receive_packet(buf, sizeof(buf));
			decodeHexToData((const char *)buf, &data_receive);
			if (rxLen == 96)
			{
				ESP_LOGI(pcTaskGetName(NULL), "Received True data from LoRa");
				data_mqtt = data_receive;
				data_mqtt.rssi_lora = lora_packet_rssi();;
				data_mqtt.snr_lora = lora_packet_snr();
			}else
			{
				ESP_LOGI(pcTaskGetName(NULL), "Received False data from LoRa");
			}
			
			xEventGroupSetBits(xEventGroup, BIT_0);
			ESP_LOGI(pcTaskGetName(NULL), "%d byte packet received:[%.*s]", rxLen, rxLen, buf);
		}
		vTaskDelay(1); // Avoid WatchDog alerts
	} // end while
}



void app_main()
{
	if (lora_init() == 0) {
		ESP_LOGE(pcTaskGetName(NULL), "Does not recognize the module");
		while(1) {
			vTaskDelay(1);
		}
	}

    xEventGroup = xEventGroupCreate();
    if (xEventGroup == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return;
    }


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

	xTaskCreatePinnedToCore(&task_rx, "RX", 1024*3, NULL, 5, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(&TaskSIM, "TaskSIM", 4096, NULL, 5, NULL, tskNO_AFFINITY);	
}

