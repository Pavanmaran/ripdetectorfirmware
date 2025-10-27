/* UART implementation with ESP32-S3 USB CDC support */

#include "uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"

// Variable to hold the MAC address
uint8_t esp_mac_address[6];

static const char *TAG = "UART";

char *uint8_t_to_char(uint8_t *data, size_t size)
{
    char *result = (char *)malloc(size + 1);
    if (result == NULL)
    {
        return NULL;
    }

    for (size_t i = 0; i < size; i++)
    {
        result[i] = (char)data[i];
    }
    result[size] = '\0';
    return result;
}

void uart_init(void)
{
#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: Initialize USB Serial JTAG for PC communication
    usb_serial_jtag_driver_config_t usb_config = {
        .rx_buffer_size = RX_BUF_SIZE * 2,
        .tx_buffer_size = RX_BUF_SIZE * 2,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_config));
    ESP_LOGI(TAG, "USB Serial JTAG initialized for PC communication");
#else
    // ESP32: Initialize UART2 for PC communication
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
    ESP_LOGI(TAG, "UART%d initialized for PC (TX: GPIO%d, RX: GPIO%d)", UART_PC_NUM, TXD_PIN_PC, RXD_PIN_PC);
#endif
}

void sendDatatoPC(const char *data)
{
    const int len = strlen(data);
    
#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: Use USB Serial JTAG
    int txBytes = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(100));
    // Don't log every transmission to avoid clutter
#else
    // ESP32: Use UART2
    int txBytes = uart_write_bytes(UART_PC_NUM, data, len);
#endif
    
    if (txBytes < len)
    {
        ESP_LOGW(TAG, "Only sent %d of %d bytes to PC", txBytes, len);
    }
}

void tx_task(void *arg)
{
    static const char *TX_TASK_TAG = "TX_TASK";
    esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
    
    while (1)
    {
        sendDatatoPC("Heartbeat\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void hex_to_hexStr(uint8_t *hexdata, size_t hexdataSize, char *hexstrData, int hexstrDataLen)
{
    if (hexstrDataLen < hexdataSize * 2 + 1)
    {
        return;
    }

    for (size_t i = 0; i < hexdataSize; ++i)
    {
        sprintf(&hexstrData[i * 2], "%02X", hexdata[i]);
    }
    hexstrData[hexdataSize * 2] = '\0';
}

void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    
    uint8_t *data = (uint8_t *)malloc(RX_BUF_SIZE + 1);
    if (data == NULL)
    {
        ESP_LOGE(RX_TASK_TAG, "Memory allocation failed for rx buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        int rxBytes = 0;
        
#ifdef CONFIG_IDF_TARGET_ESP32S3
        // ESP32-S3: Read from USB Serial JTAG
        rxBytes = usb_serial_jtag_read_bytes(data, RX_BUF_SIZE, pdMS_TO_TICKS(5000));
#else
        // ESP32: Read from UART2
        rxBytes = uart_read_bytes(UART_PC_NUM, data, RX_BUF_SIZE, pdMS_TO_TICKS(5000));
#endif

        if (rxBytes > 0)
        {
            data[rxBytes] = 0;

            ESP_LOGI(RX_TASK_TAG, "Received from PC: Read %d bytes: '%s'", rxBytes, data);
            ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);

            char *char_data = uint8_t_to_char(data, rxBytes);
            
            if (char_data != NULL)
            {
                printf("Converted data: %s\n", char_data);

                // Blink LED for 1 second
                gpio_set_level(GPIO_NUM_27, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                gpio_set_level(GPIO_NUM_27, 1);

                // Get MAC address
                esp_read_mac(esp_mac_address, ESP_MAC_WIFI_STA);
                ESP_LOGI(TAG, "ESP MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                         esp_mac_address[0], esp_mac_address[1], esp_mac_address[2],
                         esp_mac_address[3], esp_mac_address[4], esp_mac_address[5]);

                free(char_data);
            }
            else
            {
                ESP_LOGE(RX_TASK_TAG, "Memory allocation failed for char_data");
            }
        }
    }
    
    free(data);
    vTaskDelete(NULL);
}
