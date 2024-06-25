#ifndef __MQTT_H__
#define __MQTT_H__

#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "datamanager.h"

#define TIMER_ATC_PERIOD 50


#define DAM_BUF_TX 1024
#define BUF_SIZE (2048)

#define ECHO_TEST_TXD  17
#define ECHO_TEST_RXD  16
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM      2
#define ECHO_UART_BAUD_RATE     115200
#define AT_BUFFER_SZ 1024


typedef enum
{
    EVENT_OK = 0,
    EVENT_TIMEOUT,
    EVENT_ERROR,
} SIMCOM_ResponseEvent_t;

typedef void (*SIMCOM_SendATCallBack_t)(SIMCOM_ResponseEvent_t event, void *ResponseBuffer);
extern  uint8_t ATC_Sent_TimeOut;
typedef struct {
	char CMD[DAM_BUF_TX];
	uint32_t lenCMD;
	char ExpectResponseFromATC[20];
	uint32_t TimeoutATC;
	uint32_t CurrentTimeoutATC;
	uint8_t RetryCountATC;
	SIMCOM_SendATCallBack_t SendATCallBack;
}ATCommand_t;

extern char AT_BUFFER[AT_BUFFER_SZ];
extern char AT_BUFFER1[AT_BUFFER_SZ];
extern char AT_BUFFER2[AT_BUFFER_SZ];
extern SIMCOM_ResponseEvent_t AT_RX_event;
extern char *TAG_UART;
extern bool Flag_Wait_Exit; // flag for wait response from sim or Timeout and exit loop
extern bool Flag_Device_Ready ; // Flag for SIM ready to use, False: device not connect, check by use AT\r\n command
extern char *TAG_ATCommand;
extern ATCommand_t SIMCOM_ATCommand;

extern char *SIM_TAG;
void WaitandExitLoop(bool *Flag);
bool check_SIMA7670C(void);
void SendDataToServer();
void SendATCommand(void);
void RetrySendATC();
void ATC_SendATCommand(const char * Command, char * ExectResponse, uint32_t Timeout, uint8_t RetryCount, SIMCOM_SendATCallBack_t Callback);
void ATResponse_callback(SIMCOM_ResponseEvent_t event, void *ResponseBuffer);
void send_data_task_mqtt(void *arg);
void uart_rx_task_mqtt(void *arg);
void Timeout_task_mqtt(void *arg);
float calculate_checksum(Data_t *data);

#endif