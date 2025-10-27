#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include "driver/usb_serial_jtag.h"
#else
#include "driver/uart.h"
#endif

static const char *TAG = "BELT";

// ==================== HARDWARE CONFIGURATION ====================

#ifdef CONFIG_IDF_TARGET_ESP32S3
    // ESP32-S3: ADC1_CHANNEL_3 = GPIO4
    #define ADC_CHANNEL ADC1_CHANNEL_3  // GPIO4
    #define ADC_UNIT ADC_UNIT_1
    #define ADC_GPIO 4
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_27
    
    // 5V with 10K voltage divider (5V -> 2.5V)
    #define VOLTAGE_MULTIPLIER 2.0
    
#elif CONFIG_IDF_TARGET_ESP32
    // ESP32: ADC1_CHANNEL_0 = GPIO36 (VP)
    #define ADC_CHANNEL ADC1_CHANNEL_0  // GPIO36
    #define ADC_UNIT ADC_UNIT_1
    #define ADC_GPIO 36
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_2
    
    #define VOLTAGE_MULTIPLIER 2.0
    
#else
    #error "Unsupported target"
#endif

// Common Configuration
#define VOLTAGE_THRESHOLD 100       // 100mV drop threshold
#define CHECK_INTERVAL_MS 1000      // Check every 1 second
#define SEND_INTERVAL_MS 5000       // Send routine updates every 5 seconds
#define RIP_THRESHOLD_FACTOR 1.5

// ADC Configuration
#define ADC_ATTEN ADC_ATTEN_DB_11   // 0-2.5V range
#define ADC_WIDTH ADC_WIDTH_BIT_12  // 12-bit resolution

// ==================== GLOBAL VARIABLES ====================

static esp_adc_cal_characteristics_t adc_chars;
static TickType_t last_drop_time = 0;
static TickType_t prev_interval = 10000 / portTICK_PERIOD_MS;
static bool first_drop_detected = false;
static uint32_t prev_voltage = 0;
static uint8_t belt_ripped = 0;

// ==================== COMMUNICATION ====================

void init_communication(void) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "Using USB Serial JTAG (printf)");
#else
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    ESP_LOGI(TAG, "Using UART0");
#endif
}

void send_data(uint8_t status, uint16_t voltage, uint16_t interval, uint16_t time_since) {
    printf("S:%d,V:%d,I:%d,T:%d\n", status, voltage, interval, time_since);
}

// ==================== GPIO ====================

void configure_gpio(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_PIN) | (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_PIN, 1);
    gpio_set_level(RELAY_PIN, 0);
    
    ESP_LOGI(TAG, "GPIO - LED: %d, Relay: %d", LED_PIN, RELAY_PIN);
}

// ==================== ADC ====================

void initialize_adc(void) {
    // Configure ADC width first
    adc1_config_width(ADC_WIDTH);
    
    // Configure channel attenuation
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN);
    
    // Characterize ADC
    esp_adc_cal_characterize(ADC_UNIT, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
    
    ESP_LOGI(TAG, "ADC configured on GPIO%d (ADC1_CH%d)", ADC_GPIO, ADC_CHANNEL);
}

uint32_t read_voltage(void) {
    int adc_reading = 0;
    
    // Average 16 samples
    for (int i = 0; i < 16; i++) {
        int raw = adc1_get_raw(ADC_CHANNEL);
        adc_reading += raw;
    }
    adc_reading /= 16;
    
    // Convert to mV
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    
    // Apply voltage divider compensation
    uint32_t actual_voltage = (uint32_t)(voltage_mv * VOLTAGE_MULTIPLIER);
    
    return actual_voltage;
}

// ==================== LED TASK ====================

void led_task(void *arg) {
    while (1) {
        if (belt_ripped) {
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
        } else if (first_drop_detected) {
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            gpio_set_level(LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// ==================== BELT MONITORING ====================

void belt_monitor_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    ESP_LOGI(TAG, "Belt monitoring started");
    
    uint32_t voltage = read_voltage();
    prev_voltage = voltage;
    bool rip_sent = false;
    uint32_t last_send = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();
        voltage = read_voltage();
        int diff = prev_voltage - voltage;
        bool should_send = (now - last_send) >= (SEND_INTERVAL_MS / portTICK_PERIOD_MS);

        if (!first_drop_detected && diff > VOLTAGE_THRESHOLD) {
            first_drop_detected = true;
            last_drop_time = now;
            prev_voltage = voltage;
            rip_sent = false;
            ESP_LOGI(TAG, "First loop: %lu mV", (unsigned long)voltage);
            send_data(1, voltage, 0, 0);
            last_send = now;
        }
        else if (first_drop_detected && diff > VOLTAGE_THRESHOLD) {
            TickType_t interval = now - last_drop_time;
            if (interval > (CHECK_INTERVAL_MS / portTICK_PERIOD_MS)) {
                prev_interval = interval;
                last_drop_time = now;
                belt_ripped = 0;
                rip_sent = false;
                gpio_set_level(RELAY_PIN, 0);
                
                uint16_t interval_ms = interval * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "Loop OK: %u ms", interval_ms);
                send_data(2, voltage, interval_ms, 0);
                last_send = now;
                prev_voltage = voltage;
            }
        }
        else if (first_drop_detected) {
            TickType_t time_since = now - last_drop_time;
            if (time_since > (prev_interval * RIP_THRESHOLD_FACTOR)) {
                if (!rip_sent) {
                    belt_ripped = 1;
                    gpio_set_level(RELAY_PIN, 1);
                    uint16_t time_ms = time_since * portTICK_PERIOD_MS;
                    ESP_LOGE(TAG, "BELT RIPPED! %u ms", time_ms);
                    send_data(3, voltage, 0, time_ms);
                    rip_sent = true;
                    last_send = now;
                }
            }
            else if (should_send) {
                uint16_t time_ms = time_since * portTICK_PERIOD_MS;
                send_data(4, voltage, 0, time_ms);
                last_send = now;
            }
        }
        else if (should_send) {
            send_data(0, voltage, 0, 0);
            last_send = now;
        }

        prev_voltage = voltage;
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    }
}

// ==================== MAIN ====================

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Belt Monitoring System v1.0");
#ifdef CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "  Platform: ESP32-S3");
#else
    ESP_LOGI(TAG, "  Platform: ESP32");
#endif
    ESP_LOGI(TAG, "========================================");
    
    init_communication();
    configure_gpio();
    initialize_adc();
    
    uint32_t test_voltage = read_voltage();
    ESP_LOGI(TAG, "Initial voltage: %lu mV (%.2fV)", 
             (unsigned long)test_voltage, test_voltage / 1000.0);
    
    ESP_LOGI(TAG, "Hardware initialized");
    
    xTaskCreate(led_task, "led", 2048, NULL, 4, NULL);
    xTaskCreate(belt_monitor_task, "belt", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "System running");
}
