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
#endif

#define ADC_PIN ADC2_CHANNEL_0
#define RELAY_PIN GPIO_NUM_5
#define LED_PIN GPIO_NUM_27

#define VOLTAGE_THRESHOLD 100
#define CHECK_INTERVAL_MS 1000
#define SEND_INTERVAL_MS 5000
#define RIP_THRESHOLD_FACTOR 1.5

static const char *TAG = "BELT";

static esp_adc_cal_characteristics_t adc_chars;
static TickType_t last_drop_time = 0;
static TickType_t prev_interval = 10000 / portTICK_PERIOD_MS;
static bool first_drop_detected = false;
static uint32_t prev_voltage = 0;
static uint8_t belt_ripped = 0;

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
}

void initialize_adc(void) {
    adc2_config_channel_atten(ADC_PIN, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

uint32_t read_voltage(void) {
    int adc_reading = 0;
    for (int i = 0; i < 16; i++) {
        int raw;
        adc2_get_raw(ADC_PIN, ADC_WIDTH_BIT_12, &raw);
        adc_reading += raw;
    }
    adc_reading /= 16;
    return esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
}

// Simple printf - Works 100%
void send_data(uint8_t status, uint16_t voltage, uint16_t interval, uint16_t time_since) {
    // Just use printf - it goes to USB automatically on ESP32-S3
    printf("S:%d,V:%d,I:%d,T:%d\n", status, voltage, interval, time_since);
}

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
            ESP_LOGI(TAG, "First loop");
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
                uint16_t int_ms = interval * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "Loop OK");
                send_data(2, voltage, int_ms, 0);
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
                    ESP_LOGE(TAG, "BELT RIPPED!");
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

void app_main(void) {
    ESP_LOGI(TAG, "Starting Belt Monitor v1.0");
    
    configure_gpio();
    initialize_adc();
    
    ESP_LOGI(TAG, "Hardware initialized");
    
    xTaskCreate(led_task, "led", 2048, NULL, 4, NULL);
    xTaskCreate(belt_monitor_task, "belt", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Tasks started");
}
