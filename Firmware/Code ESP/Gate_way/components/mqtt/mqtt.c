#include <stdio.h>
#include "mqtt.h"
uint8_t ATC_Sent_TimeOut = 0;
char AT_BUFFER[AT_BUFFER_SZ] = "";
char AT_BUFFER1[AT_BUFFER_SZ] = "";
char AT_BUFFER2[AT_BUFFER_SZ] = "";
SIMCOM_ResponseEvent_t AT_RX_event;
char* get_received_message(void);
bool Flag_Wait_Exit = false ; // flag for wait response from sim or Timeout and exit loop
bool Flag_Device_Ready = false ; // Flag for SIM ready to use, False: device not connect, check by use AT\r\n command
ATCommand_t SIMCOM_ATCommand;
static Data_t previous_data = {0};

char time_buff[50];

char *SIM_TAG = "UART SIM";
char *TAG_UART = "UART";
char *TAG_ATCommand= "AT COMMAND";

#define SERVER "tcp://sanslab.ddns.net:1883"
#define adminmqtt "admin"
#define passmqtt "123"
char *client_id="fa2b541c1f9a49d5";

char devicename[20];

typedef struct{
	char *Server;
}Server;


Server server_sanslab =
{
		.Server = SERVER,
};
void SendATCommand()
{
	uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)SIMCOM_ATCommand.CMD, strlen(SIMCOM_ATCommand.CMD));
	ESP_LOGI(TAG_ATCommand,"Send:%s\n",SIMCOM_ATCommand.CMD);
	// ESP_LOGI(TAG_ATCommand,"Packet:\n-ExpectResponseFromATC:%s\n-RetryCountATC:%d\n-TimeoutATC:%ld\n-CurrentTimeoutATC:%ld",SIMCOM_ATCommand.ExpectResponseFromATC
			// ,SIMCOM_ATCommand.RetryCountATC,SIMCOM_ATCommand.TimeoutATC,SIMCOM_ATCommand.CurrentTimeoutATC);
	Flag_Wait_Exit = false;
}
void ATC_SendATCommand(const char * Command, char *ExpectResponse, uint32_t timeout, uint8_t RetryCount, SIMCOM_SendATCallBack_t Callback){
	strcpy(SIMCOM_ATCommand.CMD, Command);
	SIMCOM_ATCommand.lenCMD = strlen(SIMCOM_ATCommand.CMD);
	strcpy(SIMCOM_ATCommand.ExpectResponseFromATC, ExpectResponse);
	SIMCOM_ATCommand.RetryCountATC = RetryCount;
	SIMCOM_ATCommand.SendATCallBack = Callback;
	SIMCOM_ATCommand.TimeoutATC = timeout;
	SIMCOM_ATCommand.CurrentTimeoutATC = 0;
	SendATCommand();
}


void RetrySendATC(){
	SendATCommand();
}


void ATResponse_callback(SIMCOM_ResponseEvent_t event, void *ResponseBuffer){
	AT_RX_event  = event;
	if(event == EVENT_OK){
		ESP_LOGI(TAG_ATCommand, "Device is ready to use\r\n");
		Flag_Wait_Exit = true;
		Flag_Device_Ready = true;
	}
	else if(event == EVENT_TIMEOUT){
		ESP_LOGE(TAG_ATCommand, "Timeout, Device is not ready\r\n");
		Flag_Wait_Exit = true;
	}
	else if(event == EVENT_ERROR){
		ESP_LOGE(TAG_ATCommand, "AT check Error \r\n");
		Flag_Wait_Exit = true;
	}
}


void  WaitandExitLoop(bool *Flag)
{
	while(1)
	{
		if(*Flag == true)
		{
			*Flag = false;
			break;
		}
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}


bool check_SIMA7670C(void)
{
	ATC_SendATCommand("AT\r\n", "OK", 2000, 4, ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);
	if(AT_RX_event == EVENT_OK){
		return true;
	}else{
		return false;
	}
}



void Timeout_task_mqtt(void *arg)
{

	
	UBaseType_t stackSize = uxTaskGetStackHighWaterMark(NULL);
	size_t free_heap_size = esp_get_free_heap_size();	
	ESP_LOGW(TAG_UART," Remaining stack size of time out : %u, Free heap size: %zu bytes\n", stackSize, free_heap_size);
	while (1)
	{
		
		if((SIMCOM_ATCommand.TimeoutATC > 0) && (SIMCOM_ATCommand.CurrentTimeoutATC < SIMCOM_ATCommand.TimeoutATC))
		{
			SIMCOM_ATCommand.CurrentTimeoutATC += TIMER_ATC_PERIOD;
			if(SIMCOM_ATCommand.CurrentTimeoutATC >= SIMCOM_ATCommand.TimeoutATC)
			{
				SIMCOM_ATCommand.CurrentTimeoutATC -= SIMCOM_ATCommand.TimeoutATC;
				if(SIMCOM_ATCommand.RetryCountATC > 0)
				{
					ESP_LOGI(SIM_TAG,"retry count %d",SIMCOM_ATCommand.RetryCountATC-1);
					SIMCOM_ATCommand.RetryCountATC--;
					RetrySendATC();
				}
				else
				{
					if(SIMCOM_ATCommand.SendATCallBack != NULL)
					{
						printf("Time out!\n"); 

						SIMCOM_ATCommand.TimeoutATC = 0;
						SIMCOM_ATCommand.SendATCallBack(EVENT_TIMEOUT, "@@@");
						ATC_Sent_TimeOut = 1;
					}
				}
			}
		}
		vTaskDelay(TIMER_ATC_PERIOD / portTICK_PERIOD_MS);
	}
 vTaskDelete(NULL);
}

float calculate_checksum(Data_t *data)
{
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

void SendDataToServer()
{
	
	if(check_SIMA7670C())
	{
	ATC_SendATCommand("AT+NETOPEN\r\n", "OK", 2000, 2,ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);

	ATC_SendATCommand("AT+IPADDR\r\n","OK" ,2000, 2, ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);	

	ATC_SendATCommand("AT+CIPRXGET=1\r\n","OK" ,2000, 2, ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);

	ATC_SendATCommand("AT+CMQTTSTART\r\n", "OK", 2000, 2,ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);

    sprintf(AT_BUFFER ,"AT+CMQTTACCQ=0,\"%s\",0\r\n",client_id);
	ATC_SendATCommand(AT_BUFFER, "OK", 10000, 3, ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);
	memset(AT_BUFFER,0,AT_BUFFER_SZ);

	sprintf(AT_BUFFER ,"AT+CMQTTCONNECT=0,\"%s\",60,1,\"%s\",\"%s\"\r\n",server_sanslab.Server,adminmqtt,passmqtt);
	ATC_SendATCommand(AT_BUFFER, "OK", 10000, 3, ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);
    memset(AT_BUFFER,0,AT_BUFFER_SZ);

	ATC_SendATCommand("AT+CMQTTTOPIC=0,10\r\n", "OK", 2000, 2,ATResponse_callback);
	ATC_SendATCommand("modelParam", "OK", 2000, 2,ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);

    sprintf(time_buff,"%02d/%02d/%02d %02d:%02d:%02d",data_mqtt.time_data.time_day, data_mqtt.time_data.time_month,
					data_mqtt.time_data.time_year, data_mqtt.time_data.time_hour, data_mqtt.time_data.time_min, data_mqtt.time_data.time_sec);
    sprintf(devicename,"device_%d",data_mqtt.id);

    cJSON *root = cJSON_CreateObject();


    cJSON_AddStringToObject(root, "device_name",devicename);
    cJSON_AddStringToObject(root, "Time_real_Date", time_buff);
    cJSON_AddNumberToObject(root, "Soil_temp",data_mqtt.Soil_temp);
    cJSON_AddNumberToObject(root, "Soil_humi", data_mqtt.Soil_humi);
    cJSON_AddNumberToObject(root, "Soil_pH", 0);
    cJSON_AddNumberToObject(root, "Soil_Nito", 0);
    cJSON_AddNumberToObject(root, "Soil_Kali", 0);
    cJSON_AddNumberToObject(root, "Soil_Phosp",0);
    cJSON_AddNumberToObject(root, "Env_temp", data_mqtt.Env_temp);
    cJSON_AddNumberToObject(root, "Env_Humi", data_mqtt.Env_humi);
    cJSON_AddNumberToObject(root, "Env_Lux", data_mqtt.Env_lux);
	cJSON_AddNumberToObject(root, "RSSI", data_mqtt.rssi_lora);
	cJSON_AddNumberToObject(root, "SNR", data_mqtt.snr_lora);
	cJSON_AddNumberToObject(root, "Time_lora", 0);

    char *json_string = cJSON_Print(root);
	printf("%s\n", json_string);
    
	sprintf(AT_BUFFER ,"AT+CMQTTPAYLOAD=0,%d\r\n",strlen(json_string));
    ATC_SendATCommand(AT_BUFFER, "OK", 2000, 2,ATResponse_callback);
	memset(AT_BUFFER,0,AT_BUFFER_SZ);


    sprintf(AT_BUFFER,"%s\r\n",json_string);
	ATC_SendATCommand(AT_BUFFER, "OK", 2000, 2,ATResponse_callback);	
	WaitandExitLoop(&Flag_Wait_Exit);
	memset(AT_BUFFER,0,AT_BUFFER_SZ);

	ATC_SendATCommand("AT+CMQTTPUB=0,1,60\r\n", "OK", 2000, 2,ATResponse_callback);	
	WaitandExitLoop(&Flag_Wait_Exit);

	ATC_SendATCommand("AT+CMQTTDISC=0,120\r\n", "OK", 2000, 2,ATResponse_callback);	
	WaitandExitLoop(&Flag_Wait_Exit);

	ATC_SendATCommand("AT+CMQTTREL=0\r\n", "OK", 2000, 2,ATResponse_callback);	
	WaitandExitLoop(&Flag_Wait_Exit);

	ATC_SendATCommand("AT+CMQTTSTOP\r\n", "OK", 2000, 2,ATResponse_callback);	
	WaitandExitLoop(&Flag_Wait_Exit);	

	ATC_SendATCommand("AT+NETCLOSE\r\n","OK" ,2000, 2, ATResponse_callback);
	WaitandExitLoop(&Flag_Wait_Exit);
	cJSON_Delete(root);
	free(json_string);
}
}


bool has_data_changed(const Data_t* current_data, const Data_t* previous_data) {
    return memcmp(current_data, previous_data, sizeof(Data_t)) != 0;
}

void update_previous_data(Data_t* previous_data, const Data_t* current_data) {
    memcpy(previous_data, current_data, sizeof(Data_t));
}
void send_data_task_mqtt(void *arg)
{
	int count = 1;
	while (1)
	{   
		
	if (has_data_changed(&data_mqtt, &previous_data)) 
	{
      	ESP_LOGI(TAG_UART, "Ready to send server");
	    SendDataToServer();
		printf("count send:%d\n", count);
		count++;
        update_previous_data(&previous_data, &data_mqtt);
    }


		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}

void uart_rx_task_mqtt(void *arg)
{
	uart_config_t uart_config = {
		.baud_rate = ECHO_UART_BAUD_RATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_APB,
	};
	int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
	intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif
	uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, BUF_SIZE, 0, NULL, intr_alloc_flags);
	uart_param_config(ECHO_UART_PORT_NUM, &uart_config);
	uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
	uint8_t data[BUF_SIZE];

	UBaseType_t stackSize = uxTaskGetStackHighWaterMark(NULL);
	size_t free_heap_size = esp_get_free_heap_size();	
	ESP_LOGW(TAG_UART," Remaining stack size of uart sim : %u, Free heap size: %zu bytes\n", stackSize, free_heap_size);	

	while (1)
	{
		int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, BUF_SIZE, 1000 / portTICK_PERIOD_MS);
	
		if (len > 0)
		{
			data[len] = 0;
			ESP_LOGI(TAG_UART, "Rec: \r\n%s", data);
			if (SIMCOM_ATCommand.ExpectResponseFromATC[0] != 0 && strstr((const char *)data, SIMCOM_ATCommand.ExpectResponseFromATC))
			{
				SIMCOM_ATCommand.ExpectResponseFromATC[0] = 0;
				if (SIMCOM_ATCommand.SendATCallBack != NULL)
				{
					SIMCOM_ATCommand.TimeoutATC = 0;
					SIMCOM_ATCommand.SendATCallBack(EVENT_OK, data);
				}
			}
			if (strstr((const char *)data, "ERROR"))
			{

				if (SIMCOM_ATCommand.SendATCallBack != NULL)
				{
					SIMCOM_ATCommand.SendATCallBack(EVENT_ERROR, data);
				}
			}

		}
		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
	vTaskDelete(NULL);
}


