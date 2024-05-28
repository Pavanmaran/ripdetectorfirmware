#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"
#include "server_component.h"
#include "non_volatile_storage.h"
#include "server_component.h"

static const int RX_BUF_SIZE = 1024;

#define TXD_PIN_1 (GPIO_NUM_4)
#define RXD_PIN_1 (GPIO_NUM_5)
#define TXD_PIN_2 (GPIO_NUM_19)
#define RXD_PIN_2 (GPIO_NUM_18)


void uart_init(void);
int sendData(const char* logName, const char* data, uart_port_t uart_num);
void tx_task(void *arg);
void rx_task(void *arg);