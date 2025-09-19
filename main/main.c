#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"

#define ADC_PIN ADC2_CHANNEL_0     // GPIO4 for WPT receiver
#define RELAY_PIN GPIO_NUM_5       // Relay (belt rip indication)
#define gled GPIO_NUM_27           // Green LED (power indication)
#define VOLTAGE_THRESHOLD 1500     // Threshold in  (adjust based on WPT module)
#define CHECK_INTERVAL_MS 1000     // Check every 1 second for finer timing
#define STARTUP_DELAY_MS 20000     // 20 seconds startup delay
#define RIP_THRESHOLD_FACTOR 1.5   // 1.5x previous interval for rip detection
#define DEFAULT_INTERVAL_MS 10000  // Default interval before first drops
#define ADC_ATTEN ADC_ATTEN_DB_11  // 0-3.9V range
#define ADC_WIDTH ADC_WIDTH_BIT_12 // 12-bit resolution
#define RELAY_ACTIVE_LOW 0         // 1 for active-high relay
#define GLED_ACTIVE_LOW 1          // 1 for active-low green LED
#define UART1_NUM UART_NUM_1       // UART1 for logging
#define UART2_NUM UART_NUM_2       // UART2 for status/commands
#define TXD_PIN_2 GPIO_NUM_18      // UART2 TX
#define RXD_PIN_2 GPIO_NUM_19      // UART2 RX
#define RX_BUF_SIZE 128            // UART RX buffer size
#define UART2_RX_TIMEOUT_MS 5000   // 5-second timeout for RX command

static const char *TAG = "main";
static esp_adc_cal_characteristics_t adc_chars;

// Belt status
uint8_t belt_ripped = 0;
static TickType_t last_drop_time = 0;
static TickType_t prev_interval = DEFAULT_INTERVAL_MS / portTICK_PERIOD_MS;
static bool first_drop_detected = false;
static uint32_t prev_voltage = 0; // To store previous voltage reading

// Function to configure GPIO pins for LEDs
void configure_led(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN) | (1ULL << gled),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Enable pull-down for cleaner low state
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(gled, GLED_ACTIVE_LOW ? 0 : 1); // Green LED on (active-low)
    gpio_set_level(RELAY_PIN, 0); // Relay off
    ESP_LOGI(TAG, "LEDs and relay configured");
}

// Function to initialize ADC
void initialize_adc(void)
{
    adc2_config_channel_atten(ADC_PIN, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
}

// Function to read voltage from ADC pin
uint32_t read_voltage(void)
{
    int adc_reading = 0;
    for (int i = 0; i < 16; i++)
    {
        int raw;
        adc2_get_raw(ADC_PIN, ADC_WIDTH, &raw);
        adc_reading += raw;
    }
    adc_reading /= 16; // Average over 16 samples
    return esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
}

// Function to send data over UART2
void sendDatatoPC(const char *data)
{
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART2_NUM, data, len);
    ESP_LOGI(TAG, "Sent from UART %d: Wrote %d bytes: %s", UART2_NUM, txBytes, data);
}

// Function to wait for UART2 command
bool wait_for_uart2_command(void)
{
    char rx_buffer[RX_BUF_SIZE];
    int len = uart_read_bytes(UART2_NUM, (uint8_t*)rx_buffer, RX_BUF_SIZE - 1, UART2_RX_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (len > 0) {
        rx_buffer[len] = '\0'; // Null-terminate
        ESP_LOGI(TAG, "Received UART2 command: %s", rx_buffer);
        return strstr(rx_buffer, "ACK") != NULL; // Accept if "ACK" is received
    }
    ESP_LOGW(TAG, "UART2 RX timeout after %d ms", UART2_RX_TIMEOUT_MS);
    return false;
}

// Task to monitor belt status
void belt_monitor_task(void *pvParameters)
{
    char uart_buf[128];
    TickType_t current_time, time_since_last_drop;

    while (1)
    {
        current_time = xTaskGetTickCount();
        uint32_t voltage = read_voltage();

        // Initialize prev_voltage on the first reading
        if (prev_voltage == 0) {
            prev_voltage = voltage;
            snprintf(uart_buf, sizeof(uart_buf), "Initializing, Value: %ld\n", voltage);
            ESP_LOGI(TAG, "%s", uart_buf);
            sendDatatoPC(uart_buf);
        }

        int voltage_diff = prev_voltage - voltage;

        // Check for significant voltage drop as loop detection
        if (!first_drop_detected && voltage_diff > 50)
        {
            // First loop detected
            first_drop_detected = true;
            last_drop_time = current_time;
            prev_voltage = voltage; // Update to the new voltage after drop
            snprintf(uart_buf, sizeof(uart_buf), "STATUS:First Loop detected, Value: %ld\n", voltage);
            ESP_LOGI(TAG, "%s", uart_buf);
            sendDatatoPC(uart_buf);
        }
        else if (first_drop_detected && voltage_diff > 50)
        {
            // Subsequent loop detected
            TickType_t interval = current_time - last_drop_time;
            if (interval > CHECK_INTERVAL_MS / portTICK_PERIOD_MS) { // Avoid noise
                prev_interval = interval; // Update previous interval
                last_drop_time = current_time;
                belt_ripped = 0;
                gpio_set_level(RELAY_PIN, 0); // Relay off
                snprintf(uart_buf, sizeof(uart_buf), "STATUS:Loop detected, Value: %ld, Interval: %ld\n",
                         voltage, interval * portTICK_PERIOD_MS);
                ESP_LOGI(TAG, "%s", uart_buf);
                sendDatatoPC(uart_buf);
                prev_voltage = voltage; // Update to the new voltage after drop
            }
        }
        else if (first_drop_detected)
        {
            // No loop detected: check if time exceeds threshold for rip detection
            time_since_last_drop = current_time - last_drop_time;
            if (time_since_last_drop > (TickType_t)(prev_interval * RIP_THRESHOLD_FACTOR))
            {
                belt_ripped = 1;
                gpio_set_level(RELAY_PIN, 1); // Relay on (active high)
                gpio_set_level(gled, 1); // Blink LED (active low, turn off)
                snprintf(uart_buf, sizeof(uart_buf), "STATUS:Belt RIPPED, Value: %ld, %ld\n",
                         voltage, time_since_last_drop * portTICK_PERIOD_MS);
                ESP_LOGE(TAG, "%s", uart_buf);
                sendDatatoPC(uart_buf);
                vTaskDelay(100); // Short blink
                gpio_set_level(gled, 0); // LED back on
            }
            else
            {
                snprintf(uart_buf, sizeof(uart_buf), "No Loop Detecting, Value: %ld, %ld\n",
                         voltage, time_since_last_drop * portTICK_PERIOD_MS);
                ESP_LOGI(TAG, "%s", uart_buf);
                sendDatatoPC(uart_buf);
            }
        }
        else
        {
            snprintf(uart_buf, sizeof(uart_buf), "Waiting for Loop detection, Value: %ld\n", voltage);
            ESP_LOGI(TAG, "%s", uart_buf);
            sendDatatoPC(uart_buf);
        }

        prev_voltage = voltage; // Update previous voltage after all checks

        // Wait for next check
        vTaskDelay(CHECK_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

// Function to initialize UART
void uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Initialize UART1 (logging, default pins: GPIO1 TX, GPIO3 RX)
    ESP_ERROR_CHECK(uart_driver_install(UART1_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART1_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART1_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART1 initialized (logging, default pins)");

    // Initialize UART2 (status/commands, GPIO19 TX, GPIO18 RX)
    ESP_ERROR_CHECK(uart_driver_install(UART2_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART2_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART2_NUM, TXD_PIN_2, RXD_PIN_2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART2 initialized (TX: GPIO19, RX: GPIO18)");
}

void initialize_uart(void)
{
    uart_init();
    uart_port_t uart2 = UART2_NUM;
    xTaskCreate(belt_monitor_task, "belt_monitor_task", 4096, NULL, 5, NULL);
    // xTaskCreate(rx_task, "uart_rx_task_2", 1024 * 8, &uart2, configMAX_PRIORITIES - 1, NULL);
}

// Function to initialize NVS
void initialize_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// Application main function
void app_main(void)
{
    // Configure LEDs
    configure_led();

    // Initialize NVS
    initialize_nvs();
    ESP_LOGI(TAG, "NVS initialized");

    // Initialize ADC
    initialize_adc();
    ESP_LOGI(TAG, "ADC initialized");

    // Initialize UART
    initialize_uart();
    ESP_LOGI(TAG, "UART initialized");

    // Startup delay
    ESP_LOGI(TAG, "Waiting %ld ms for belt to reach speed", STARTUP_DELAY_MS);
    vTaskDelay(STARTUP_DELAY_MS / portTICK_PERIOD_MS);

    // Create belt monitoring task
    // xTaskCreate(belt_monitor_task, "belt_monitor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialization complete");
}