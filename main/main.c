#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include "uart.h"  // Include uart.h to use sendDatatoPC

#define ADC_PIN ADC2_CHANNEL_0     // GPIO4 for WPT receiver
#define RELAY_PIN GPIO_NUM_5       // Relay (belt rip indication)
#define gled GPIO_NUM_27           // Green LED (power indication)

#define VOLTAGE_THRESHOLD 100     // Threshold in mV
#define CHECK_INTERVAL_MS 1000     // Check every 1 second
#define STARTUP_DELAY_MS 20000     // 20 seconds startup delay
#define RIP_THRESHOLD_FACTOR 1.5   // 1.5x previous interval for rip detection
#define DEFAULT_INTERVAL_MS 10000  // Default interval before first drops
#define ADC_ATTEN ADC_ATTEN_DB_11  // 0-3.9V range
#define ADC_WIDTH ADC_WIDTH_BIT_12 // 12-bit resolution
#define RELAY_ACTIVE_LOW 0         // 0 for active-high relay
#define GLED_ACTIVE_LOW 1          // 1 for active-low green LED

static const char *TAG = "main";
static esp_adc_cal_characteristics_t adc_chars;

// Belt status
uint8_t belt_ripped = 0;
static TickType_t last_drop_time = 0;
static TickType_t prev_interval = DEFAULT_INTERVAL_MS / portTICK_PERIOD_MS;
static bool first_drop_detected = false;
static uint32_t prev_voltage = 0;

// Enum for indicators
enum {
    LED_ON,
    LED_OFF,
    RIP_INDICATOR
} indicator;

// Function to configure GPIO pins for LEDs
void configure_led(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN) | (1ULL << gled),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(gled, GLED_ACTIVE_LOW ? 1 : 0); // Green LED on (active-low)
    gpio_set_level(RELAY_PIN, 0); // Relay off
    ESP_LOGI(TAG, "LEDs and relay configured");
}

// Task to handle multiple indicators
void EventIndicatorTask(void *pvParameters) {
    while (1) {
        if (indicator == LED_ON) {
            gpio_set_level(gled, GLED_ACTIVE_LOW ? 0 : 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (indicator == LED_OFF) {
            gpio_set_level(gled, GLED_ACTIVE_LOW ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (indicator == RIP_INDICATOR) {
            while (indicator == RIP_INDICATOR) {
                gpio_set_level(gled, GLED_ACTIVE_LOW ? 1 : 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_set_level(gled, GLED_ACTIVE_LOW ? 0 : 1);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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
    adc_reading /= 16;
    return esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
}

void belt_monitor_task(void *pvParameters) {
    char uart_buf[128];
    TickType_t current_time, time_since_last_drop;

    // Wait for initial ADC reading
    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t voltage = read_voltage();
    prev_voltage = voltage;

    while (1) {
        current_time = xTaskGetTickCount();
        voltage = read_voltage();
        int voltage_diff = prev_voltage - voltage;

        if (!first_drop_detected && voltage_diff > VOLTAGE_THRESHOLD) {
            first_drop_detected = true;
            last_drop_time = current_time;
            prev_voltage = voltage;
            snprintf(uart_buf, sizeof(uart_buf), "STATUS:First Loop detected, Value: %lu\n", (unsigned long)voltage);
            ESP_LOGI(TAG, "%s", uart_buf);
            sendDatatoPC(uart_buf);
            indicator = LED_OFF;
        }
        else if (first_drop_detected && voltage_diff > VOLTAGE_THRESHOLD) {
            TickType_t interval = current_time - last_drop_time;
            if (interval > CHECK_INTERVAL_MS / portTICK_PERIOD_MS) {
                prev_interval = interval;
                last_drop_time = current_time;
                belt_ripped = 0;
                gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 1 : 0);
                snprintf(uart_buf, sizeof(uart_buf), "STATUS:Loop detected, Value: %lu, Interval: %lu\n",
                         (unsigned long)voltage, (unsigned long)(interval * portTICK_PERIOD_MS));
                ESP_LOGI(TAG, "%s", uart_buf);
                sendDatatoPC(uart_buf);
                indicator = LED_OFF;
                prev_voltage = voltage;
            }
        }
        else if (first_drop_detected) {
            time_since_last_drop = current_time - last_drop_time;
            if (time_since_last_drop > (TickType_t)(prev_interval * RIP_THRESHOLD_FACTOR)) {
                belt_ripped = 1;
                gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 0 : 1);
                snprintf(uart_buf, sizeof(uart_buf), "STATUS:Belt RIPPED, Value: %lu, Time: %lu\n",
                         (unsigned long)voltage, (unsigned long)(time_since_last_drop * portTICK_PERIOD_MS));
                ESP_LOGE(TAG, "%s", uart_buf);
                sendDatatoPC(uart_buf);
                indicator = RIP_INDICATOR;
            }
            else {
                snprintf(uart_buf, sizeof(uart_buf), "STATUS:No Loop Detecting, Value: %lu, Time: %lu\n",
                         (unsigned long)voltage, (unsigned long)(time_since_last_drop * portTICK_PERIOD_MS));
                ESP_LOGI(TAG, "%s", uart_buf);
                sendDatatoPC(uart_buf);
                indicator = LED_ON;
            }
        }
        else {
            snprintf(uart_buf, sizeof(uart_buf), "STATUS:Waiting for Loop detection, Value: %lu\n", (unsigned long)voltage);
            ESP_LOGI(TAG, "%s", uart_buf);
            sendDatatoPC(uart_buf);
            indicator = LED_ON;
        }

        prev_voltage = voltage;
        vTaskDelay(CHECK_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

// Application main function
void app_main(void)
{
    // Configure LEDs
    configure_led();

    // Initialize ADC
    initialize_adc();
    ESP_LOGI(TAG, "ADC initialized");

    // Initialize UART (single function call)
    uart_init();
    ESP_LOGI(TAG, "UART initialized");

    // Create event indicator task
    xTaskCreate(EventIndicatorTask, "EventIndicatorTask", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "EventIndicatorTask created");

    // Startup delay
    ESP_LOGI(TAG, "Waiting %d ms for belt to reach speed", STARTUP_DELAY_MS);
    vTaskDelay(STARTUP_DELAY_MS / portTICK_PERIOD_MS);

    // Create belt monitoring task
    xTaskCreate(belt_monitor_task, "belt_monitor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialization complete");
}
