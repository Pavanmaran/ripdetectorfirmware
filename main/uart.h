#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
    #include "driver/usb_serial_jtag.h"
#endif

static const int RX_BUF_SIZE = 1024;

// Pin definitions based on chip type
#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: USB Serial JTAG for PC (built-in, no pins needed)
    // No TXD/RXD pins needed for USB
#else
    // ESP32: UART for PC communication
    #define TXD_PIN_PC (GPIO_NUM_19)
    #define RXD_PIN_PC (GPIO_NUM_18)
    #define UART_PC_NUM UART_NUM_2

    // #define TXD_PIN_1 (GPIO_NUM_17)
    // #define RXD_PIN_1 (GPIO_NUM_16)
#endif

void uart_init(void);
void sendDatatoPC(const char *data);
void tx_task(void *arg);
void rx_task(void *arg);
