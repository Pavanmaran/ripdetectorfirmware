#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
// #include "nvs_flash.h"
// #include "nvs.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include "driver/usb_serial_jtag.h"
#include "driver/uart.h"
#else
#include "driver/uart.h"
#endif

static const char *TAG = "BELT";

// ==================== PLATFORM-SPECIFIC CONFIGURATION ====================

#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3 Configuration
    #define ADC_PIN ADC1_CHANNEL_3      // GPIO4
    #define ADC_UNIT_NUM ADC_UNIT_1
    #define USE_ADC1 1
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_9
    #define UART_PC_NUM UART_NUM_0      // USB Serial JTAG uses UART0
    #define TXD_PIN_PC UART_PIN_NO_CHANGE
    #define RXD_PIN_PC UART_PIN_NO_CHANGE
    #define VOLTAGE_MULTIPLIER 2.0      // For 10K voltage divider
#else
    // ESP32 Configuration (Your existing hardware)
    #define ADC_PIN ADC2_CHANNEL_0      // GPIO4
    #define ADC_UNIT_NUM ADC_UNIT_2
    #define USE_ADC1 0
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_27         // gled
    #define UART_PC_NUM UART_NUM_2      // UART2 for PC communication
    #define TXD_PIN_PC GPIO_NUM_18      // TX on GPIO18
    #define RXD_PIN_PC GPIO_NUM_19      // RX on GPIO19
    #define VOLTAGE_MULTIPLIER 2.0
#endif

// Common Configuration (from your working code)
#define VOLTAGE_THRESHOLD 100
#define CHECK_INTERVAL_MS 1000
#define SEND_INTERVAL_MS 5000
#define RIP_THRESHOLD_FACTOR 1.5
#define DEFAULT_INTERVAL_MS 10000
#define ADC_ATTEN ADC_ATTEN_DB_11
#define ADC_WIDTH ADC_WIDTH_BIT_12
#define RELAY_ACTIVE_LOW 0
#define LED_ACTIVE_LOW 1
#define RX_BUF_SIZE 128

// ==================== GLOBAL VARIABLES ====================

static esp_adc_cal_characteristics_t adc_chars;
static TickType_t last_drop_time = 0;
static TickType_t prev_interval = DEFAULT_INTERVAL_MS / portTICK_PERIOD_MS;
static bool first_drop_detected = false;
static uint32_t prev_voltage = 0;
static uint8_t belt_ripped = 0;
static uint32_t last_send_time = 0;

enum {
    LED_ON,
    LED_OFF,
    RIP_INDICATOR
} indicator = LED_ON;

// ==================== NVS INITIALIZATION ====================

void initialize_nvs(void) {
    // esp_err_t ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NOT_FOUND) {
    //     ESP_ERROR_CHECK(nvs_flash_erase());
    //     ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);
    // ESP_LOGI(TAG, "NVS initialized");
}

// ==================== GPIO CONFIGURATION ====================

void configure_led(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN) | (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 0 : 1);
    gpio_set_level(RELAY_PIN, 0);
    ESP_LOGI(TAG, "GPIO configured - LED: %d, Relay: %d", LED_PIN, RELAY_PIN);
}

// ==================== ADC CONFIGURATION ====================

void initialize_adc(void) {
#if USE_ADC1
    // ESP32-S3 uses ADC1
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_PIN, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_NUM, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
    ESP_LOGI(TAG, "ADC1 initialized on GPIO4");
#else
    // ESP32 uses ADC2
    adc2_config_channel_atten(ADC_PIN, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_NUM, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
    ESP_LOGI(TAG, "ADC2 initialized on GPIO4");
#endif
}

uint32_t read_voltage(void) {
    int adc_reading = 0;
    for (int i = 0; i < 16; i++) {
        int raw;
#if USE_ADC1
        raw = adc1_get_raw(ADC_PIN);
#else
        adc2_get_raw(ADC_PIN, ADC_WIDTH, &raw);
#endif
        adc_reading += raw;
    }
    adc_reading /= 16;
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    return (uint32_t)(voltage_mv * VOLTAGE_MULTIPLIER);
}

// ==================== UART COMMUNICATION ====================

void uart_init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: printf automatically goes to USB Serial JTAG
    ESP_LOGI(TAG, "Using USB Serial JTAG for PC communication");
#else
    // ESP32: Initialize UART2 for PC communication
    ESP_ERROR_CHECK(uart_driver_install(UART_PC_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PC_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PC_NUM, TXD_PIN_PC, RXD_PIN_PC, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART2 initialized (TX: GPIO%d, RX: GPIO%d)", TXD_PIN_PC, RXD_PIN_PC);
#endif
}

void sendDatatoPC(const char *data) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: Use printf (goes to USB Serial JTAG)
    printf("%s", data);
#else
    // ESP32: Use UART2
    const int len = strlen(data);
    uart_write_bytes(UART_PC_NUM, data, len);
#endif
}

// ==================== LED INDICATOR TASK ====================

void EventIndicatorTask(void *pvParameters) {
    while (1) {
        if (indicator == LED_ON) {
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 0 : 1);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (indicator == LED_OFF) {
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (indicator == RIP_INDICATOR) {
            while (indicator == RIP_INDICATOR) {
                gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 0 : 1);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ==================== BELT MONITORING TASK ====================

void belt_monitor_task(void *pvParameters) {
    char uart_buf[128];
    TickType_t current_time, time_since_last_drop;
    bool rip_sent = false;

    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t voltage = read_voltage();
    prev_voltage = voltage;

    while (1) {
        current_time = xTaskGetTickCount();
        voltage = read_voltage();
        int voltage_diff = prev_voltage - voltage;
        
        bool should_send = (current_time - last_send_time) >= (SEND_INTERVAL_MS / portTICK_PERIOD_MS);

        // First loop detection
        if (!first_drop_detected && voltage_diff > VOLTAGE_THRESHOLD) {
            first_drop_detected = true;
            last_drop_time = current_time;
            prev_voltage = voltage;
            rip_sent = false;
            
            snprintf(uart_buf, sizeof(uart_buf), "S:1,V:%lu,I:0,T:0\n", (unsigned long)voltage);
            ESP_LOGI(TAG, "First loop: %lu mV", (unsigned long)voltage);
            sendDatatoPC(uart_buf);
            last_send_time = current_time;
            indicator = LED_OFF;
        }
        // Loop detection (belt OK)
        else if (first_drop_detected && voltage_diff > VOLTAGE_THRESHOLD) {
            TickType_t interval = current_time - last_drop_time;
            if (interval > CHECK_INTERVAL_MS / portTICK_PERIOD_MS) {
                prev_interval = interval;
                last_drop_time = current_time;
                belt_ripped = 0;
                rip_sent = false;
                gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 1 : 0);
                
                uint32_t interval_ms = interval * portTICK_PERIOD_MS;
                snprintf(uart_buf, sizeof(uart_buf), "S:2,V:%lu,I:%lu,T:0\n", 
                         (unsigned long)voltage, (unsigned long)interval_ms);
                ESP_LOGI(TAG, "Loop OK: %lu ms", (unsigned long)interval_ms);
                sendDatatoPC(uart_buf);
                last_send_time = current_time;
                indicator = LED_OFF;
                prev_voltage = voltage;
            }
        }
        // Check for belt rip
        else if (first_drop_detected) {
            time_since_last_drop = current_time - last_drop_time;
            if (time_since_last_drop > (TickType_t)(prev_interval * RIP_THRESHOLD_FACTOR)) {
                if (!rip_sent) {
                    belt_ripped = 1;
                    gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 0 : 1);
                    
                    uint32_t time_ms = time_since_last_drop * portTICK_PERIOD_MS;
                    snprintf(uart_buf, sizeof(uart_buf), "S:3,V:%lu,I:0,T:%lu\n",
                             (unsigned long)voltage, (unsigned long)time_ms);
                    ESP_LOGE(TAG, "BELT RIPPED! %lu ms", (unsigned long)time_ms);
                    sendDatatoPC(uart_buf);
                    rip_sent = true;
                    last_send_time = current_time;
                }
                indicator = RIP_INDICATOR;
            }
            else if (should_send) {
                uint32_t time_ms = time_since_last_drop * portTICK_PERIOD_MS;
                snprintf(uart_buf, sizeof(uart_buf), "S:4,V:%lu,I:0,T:%lu\n",
                         (unsigned long)voltage, (unsigned long)time_ms);
                sendDatatoPC(uart_buf);
                last_send_time = current_time;
                indicator = LED_ON;
            }
        }
        // Waiting for loop
        else if (should_send) {
            snprintf(uart_buf, sizeof(uart_buf), "S:0,V:%lu,I:0,T:0\n", (unsigned long)voltage);
            sendDatatoPC(uart_buf);
            last_send_time = current_time;
            indicator = LED_ON;
        }

        prev_voltage = voltage;
        vTaskDelay(CHECK_INTERVAL_MS / portTICK_PERIOD_MS);
    }
}

// ==================== MAIN APPLICATION ====================

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Belt Monitoring System v1.0");
#ifdef CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "  Platform: ESP32-S3");
#else
    ESP_LOGI(TAG, "  Platform: ESP32");
#endif
    ESP_LOGI(TAG, "========================================");

    // Initialize NVS
    initialize_nvs();

    // Configure GPIO
    configure_led();

    // Initialize ADC
    initialize_adc();

    // Initialize UART
    uart_init();

    // Test voltage reading
    uint32_t test_voltage = read_voltage();
    ESP_LOGI(TAG, "Initial voltage: %lu mV (%.2fV)", 
             (unsigned long)test_voltage, test_voltage / 1000.0);

    // Create tasks
    xTaskCreate(EventIndicatorTask, "EventIndicatorTask", 4096, NULL, 5, NULL);
    xTaskCreate(belt_monitor_task, "belt_monitor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System initialized - all tasks running");
}
