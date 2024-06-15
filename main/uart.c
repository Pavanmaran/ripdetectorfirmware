/* UART asynchronous example, that uses separate RX and TX tasks for two UART ports

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"

// Variable to hold the MAC address
uint8_t esp_mac_address[6]; // MAC address is 6 bytes long

#define HEX2STR
static const char *TAG = "UART";

char *uint8_t_to_char(uint8_t *data, size_t size)
{
    // Allocate memory for the char array
    char *result = (char *)malloc(size + 1); // +1 for null terminator

    // Check if memory allocation was successful
    if (result == NULL)
    {
        return NULL; // Allocation failed
    }

    // Copy each element of the uint8_t array to the char array
    for (size_t i = 0; i < size; i++)
    {
        result[i] = (char)data[i];
    }

    // Add null terminator at the end of the char array
    result[size] = '\0';

    return result;
}

void uart_init(void)
{
    const uart_config_t uart_config = {
        // .baud_rate = 9600,
        .baud_rate = 2400,  // for meteler only
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // Initialize and configure the first UART port
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN_1, RXD_PIN_1, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Initialize and configure the second UART port
    uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_2, &uart_config);
    uart_set_pin(UART_NUM_2, TXD_PIN_2, RXD_PIN_2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // get esp mac address
}

int sendData(const char *logName, const char *data, uart_port_t uart_num)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(uart_num, data, len);
    ESP_LOGI(logName, "Sent from UART %d: Wrote %d bytes", uart_num, txBytes);
    return txBytes;
}

void tx_task(void *arg)
{
    static const char *TX_TASK_TAG = "TX_TASK";
    esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
    uart_port_t uart_num = *((uart_port_t *)arg);
    while (1)
    {
        sendData(TX_TASK_TAG, "Hello world", uart_num);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
}

void hex_to_hexStr(uint8_t *hexdata, size_t hexdataSize, char *hexstrData, int hexstrDataLen) {
    if (hexstrDataLen < hexdataSize * 2 + 1) {
        // Not enough space in hexstrData to store the hex string
        return;
    }

    for (size_t i = 0; i < hexdataSize; ++i) {
        sprintf(&hexstrData[i * 2], "%02X", hexdata[i]);
    }

    hexstrData[hexdataSize * 2] = '\0'; // Null-terminate the string
}

void rx_task(void *arg)
{
    dataLoggerConfig read_config_struct;
    if (read_config_from_NVS(&read_config_struct) == 0)
    {
        ESP_LOGI("Error NVS Reading", "The DATALOGGER is not configured yet");
    }
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t *data = (uint8_t *)malloc(RX_BUF_SIZE + 1);
    uart_port_t uart_num = *((uart_port_t *)arg);
    while (1)
    {
        const int rxBytes = uart_read_bytes(uart_num, data, RX_BUF_SIZE, 5000 / portTICK_PERIOD_MS);
        if (rxBytes > 0 )
        {
            data[rxBytes] = 0;
                        
            ESP_LOGI(RX_TASK_TAG, "Received on UART %d: Read %d bytes: '%s'", uart_num, rxBytes, data);
            ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);
#ifndef HEX2STR
            // convert uint8_t array to char array
            char *char_data = uint8_t_to_char(data, RX_BUF_SIZE);
#endif
#ifdef HEX2STR
            // Allocate space for the hex string
            char char_data[2 * RX_BUF_SIZE + 1];
            hex_to_hexStr(data, rxBytes, char_data, sizeof(char_data));
            printf("Hex string: %s\n", char_data);
#endif   
            if (rxBytes>0)
            {
                printf("Converted data: %s\n", char_data);

                // blink gled for 1 second
                gpio_set_level(GPIO_NUM_27, 0);
                vTaskDelay(pdMS_TO_TICKS(1000)); 
                gpio_set_level(GPIO_NUM_27, 1);

                if (uart_num == 2)
                {
                    // send data to server with mac
                    esp_read_mac(esp_mac_address, ESP_MAC_WIFI_STA);
                    ESP_LOGI(TAG, "ESP MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                             esp_mac_address[0], esp_mac_address[1], esp_mac_address[2],
                             esp_mac_address[3], esp_mac_address[4], esp_mac_address[5]);

                    char mac_str[18]; // String to hold MAC address
                    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
                            esp_mac_address[0], esp_mac_address[1], esp_mac_address[2],
                            esp_mac_address[3], esp_mac_address[4], esp_mac_address[5]);
                    // create json
                       char* data = createJSON("IOT DATA LOGGER", mac_str, char_data);

                    post_rest_function(read_config_struct.URL, data);
                    free(data);
                }
#ifndef HEX2STR
                free(char_data); // Don't forget to free the memory
#endif
            }
            else
            {
                printf("Memory allocation failed.\n");
            }
        }
    }
    free(data);
}

// void app_main(void)
// {
//     uart_init();
//     uart_port_t uart1 = UART_NUM_1;
//     uart_port_t uart2 = UART_NUM_2;
//     xTaskCreate(rx_task, "uart_rx_task_1", 1024 * 8, &uart1, configMAX_PRIORITIES - 1, NULL);
//     xTaskCreate(tx_task, "uart_tx_task_1", 1024 * 2, &uart1, configMAX_PRIORITIES - 2, NULL);
//     xTaskCreate(rx_task, "uart_rx_task_2", 1024 * 8, &uart2, configMAX_PRIORITIES - 1, NULL);
//     xTaskCreate(tx_task, "uart_tx_task_2", 1024 * 2, &uart2, configMAX_PRIORITIES - 2, NULL);
// }
