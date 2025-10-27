/* UART implementation with ESP32-S3 USB CDC support */

#include "uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"

uint8_t esp_mac_address[6];
static const char *TAG = "UART";

void uart_init(void)
{
#ifdef CONFIG_IDF_TARGET_ESP32S3
    usb_serial_jtag_driver_config_t usb_config = {
        .rx_buffer_size = RX_BUF_SIZE * 2,
        .tx_buffer_size = RX_BUF_SIZE * 2,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    ESP_LOGI(TAG, "USB Serial JTAG initialized for PC communication");
#else
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ESP_ERROR_CHECK(uart_driver_install(UART_PC_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PC_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PC_NUM, TXD_PIN_PC, RXD_PIN_PC, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART%d initialized for PC", UART_PC_NUM);
#endif
}

// UPDATED: Accept binary data
void sendDatatoPC(const void *data, size_t len)
{
#ifdef CONFIG_IDF_TARGET_ESP32S3
    usb_serial_jtag_write_bytes((const char*)data, len, pdMS_TO_TICKS(100));
#else
    uart_write_bytes(UART_PC_NUM, (const char*)data, len);
#endif
}
