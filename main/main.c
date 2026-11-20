#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

// Honda, andoid, interface, 3 pages, 4 page, amount of wastage, 1kg, 2kg, 3kg.
// ADC includes
#include "driver/adc.h"
#include "esp_adc_cal.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include "driver/usb_serial_jtag.h"
#include "driver/uart.h"
#else
#include "driver/uart.h"
#endif

static const char *TAG = "BELT";

// ==================== ADC CONFIGURATION (GPIO16) ====================
// ESP32-S3 GPIO16 = ADC2_CH5
#define ADC_PIN ADC2_CHANNEL_5  // GPIO16
#define ADC_UNIT_NUM ADC_UNIT_2
#define ADC_ATTEN ADC_ATTEN_DB_11  // 0-3.3V range
#define ADC_WIDTH ADC_WIDTH_BIT_12  // 12-bit

// Voltage divider: 10kΩ + 4.7kΩ
// Divider ratio: 4.7/(10+4.7) = 0.32
#define VOLTAGE_MULTIPLIER 3.125  // 1/0.32

static esp_adc_cal_characteristics_t adc_chars;

// ==================== GPIO PINS ====================
#ifdef CONFIG_IDF_TARGET_ESP32S3
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_11
    #define UART_PC_NUM UART_NUM_0
    #define TXD_PIN_PC UART_PIN_NO_CHANGE
    #define RXD_PIN_PC UART_PIN_NO_CHANGE
#else
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_27
    #define UART_PC_NUM UART_NUM_2
    #define TXD_PIN_PC GPIO_NUM_18
    #define RXD_PIN_PC GPIO_NUM_19
#endif

// ==================== CONFIGURATION ====================
#define VOLTAGE_DROP_THRESHOLD_MV 100.0  // 100mV drop detection
#define CHECK_INTERVAL_MS 500
#define SEND_INTERVAL_MS 500
#define RIP_THRESHOLD_FACTOR 1.5
#define DEFAULT_INTERVAL_MS 10000

#define RELAY_ACTIVE_LOW 0
#define LED_ACTIVE_LOW 1
#define RX_BUF_SIZE 128

// ==================== GLOBAL VARIABLES ====================
static TickType_t last_drop_time = 0;
static TickType_t prev_interval = DEFAULT_INTERVAL_MS / portTICK_PERIOD_MS;
static bool first_drop_detected = false;
static float prev_voltage_mv = 0.0;
static uint8_t belt_ripped = 0;
static TickType_t last_send_time = 0;

enum {
    LED_ON,
    LED_OFF,
    RIP_INDICATOR
} indicator = LED_ON;

// ==================== GPIO SETUP ====================
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
    gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 1 : 0);
    
    ESP_LOGI(TAG, "GPIO configured - LED: %d, Relay: %d", LED_PIN, RELAY_PIN);
}

// ==================== ADC SETUP ====================
void initialize_adc(void) {
    adc2_config_channel_atten(ADC_PIN, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_NUM, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
    ESP_LOGI(TAG, "ADC initialized - GPIO16 (ADC2_CH5)");
}

// ==================== ADC READ ====================
float read_voltage_adc_mv(void) {
    int adc_reading = 0;
    int raw = 0;
    
    // 16x oversampling
    for (int i = 0; i < 16; i++) {
        if (adc2_get_raw(ADC_PIN, ADC_WIDTH, &raw) == ESP_OK) {
            adc_reading += raw;
        } else {
            return prev_voltage_mv;
        }
    }
    
    adc_reading /= 16;
    uint32_t voltage_adc_mv = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    float actual_voltage_mv = voltage_adc_mv * VOLTAGE_MULTIPLIER;
    
    return actual_voltage_mv;
}

// ==================== UART ====================
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
    ESP_LOGI(TAG, "Using USB Serial JTAG");
#else
    ESP_ERROR_CHECK(uart_driver_install(UART_PC_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PC_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PC_NUM, TXD_PIN_PC, RXD_PIN_PC, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
#endif
}

void sendDatatoPC(const char *data) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    printf("%s", data);
#else
    uart_write_bytes(UART_PC_NUM, data, strlen(data));
#endif
}

// ==================== LED TASK ====================
void EventIndicatorTask(void *pvParameters) {
    while (1) {
        if (indicator == LED_ON) {
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        } 
        else if (indicator == LED_OFF) {
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 0 : 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            indicator = LED_ON;
        } 
        else if (indicator == RIP_INDICATOR) {
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(150));
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 0 : 1);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==================== BELT MONITOR ====================
void belt_monitor_task(void *pvParameters) {
    char uart_buf[128];
    TickType_t current_time, time_since_last_drop;
    bool rip_sent = false;

    vTaskDelay(pdMS_TO_TICKS(1000));
    
    float voltage_mv = read_voltage_adc_mv();
    prev_voltage_mv = voltage_mv;
    
    ESP_LOGI(TAG, "Baseline: %.2f mV", voltage_mv);

    while (1) {
        current_time = xTaskGetTickCount();
        voltage_mv = read_voltage_adc_mv();
        float voltage_diff = prev_voltage_mv - voltage_mv;
        
        bool should_send = (current_time - last_send_time) >= (SEND_INTERVAL_MS / portTICK_PERIOD_MS);
        
        ESP_LOGI(TAG, "V=%.2f mV, Diff=%.2f mV", voltage_mv, voltage_diff);
        
        // FIRST LOOP
        if (!first_drop_detected && voltage_diff > VOLTAGE_DROP_THRESHOLD_MV) {
            first_drop_detected = true;
            last_drop_time = current_time;
            prev_voltage_mv = voltage_mv;
            rip_sent = false;
            belt_ripped = 0;
            
            snprintf(uart_buf, sizeof(uart_buf), "S:1,V:%.0f,I:0,T:0\n", voltage_mv);
            sendDatatoPC(uart_buf);
            ESP_LOGI(TAG, "✓ FIRST LOOP: %.2f mV", voltage_mv);
            
            indicator = LED_OFF;
            last_send_time = current_time;
        }
        // LOOP OK
        else if (first_drop_detected && voltage_diff > VOLTAGE_DROP_THRESHOLD_MV) {
            TickType_t interval = current_time - last_drop_time;
            
            if (interval > CHECK_INTERVAL_MS / portTICK_PERIOD_MS) {
                prev_interval = interval;
                last_drop_time = current_time;
                belt_ripped = 0;
                rip_sent = false;
                
                gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 1 : 0);
                
                uint32_t interval_ms = interval * portTICK_PERIOD_MS;
                snprintf(uart_buf, sizeof(uart_buf), "S:2,V:%.0f,I:%lu,T:0\n", 
                         voltage_mv, (unsigned long)interval_ms);
                sendDatatoPC(uart_buf);
                ESP_LOGI(TAG, "✓ LOOP OK: I=%lu ms", (unsigned long)interval_ms);
                
                indicator = LED_OFF;
                last_send_time = current_time;
                prev_voltage_mv = voltage_mv;
            }
        }
        // RIP DETECTION
        else if (first_drop_detected) {
            time_since_last_drop = current_time - last_drop_time;
            
            if (time_since_last_drop > (TickType_t)(prev_interval * RIP_THRESHOLD_FACTOR)) {
                if (!rip_sent) {
                    belt_ripped = 1;
                    gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 0 : 1);
                    
                    uint32_t time_ms = time_since_last_drop * portTICK_PERIOD_MS;
                    snprintf(uart_buf, sizeof(uart_buf), "S:3,V:%.0f,I:0,T:%lu\n",
                             voltage_mv, (unsigned long)time_ms);
                    sendDatatoPC(uart_buf);
                    ESP_LOGE(TAG, "⚠️ BELT RIPPED! Time: %lu ms", (unsigned long)time_ms);
                    
                    rip_sent = true;
                    last_send_time = current_time;
                }
                
                if (should_send) {
                    uint32_t time_ms = time_since_last_drop * portTICK_PERIOD_MS;
                    snprintf(uart_buf, sizeof(uart_buf), "S:3,V:%.0f,I:0,T:%lu\n",
                             voltage_mv, (unsigned long)time_ms);
                    sendDatatoPC(uart_buf);
                    last_send_time = current_time;
                }
                
                indicator = RIP_INDICATOR;
            }
            else if (should_send) {
                uint32_t time_ms = time_since_last_drop * portTICK_PERIOD_MS;
                snprintf(uart_buf, sizeof(uart_buf), "S:4,V:%.0f,I:0,T:%lu\n",
                         voltage_mv, (unsigned long)time_ms);
                sendDatatoPC(uart_buf);
                last_send_time = current_time;
                indicator = LED_ON;
            }
        }
        // WAITING
        else if (should_send) {
            snprintf(uart_buf, sizeof(uart_buf), "S:0,V:%.0f,I:0,T:0\n", voltage_mv);
            sendDatatoPC(uart_buf);
            last_send_time = current_time;
            indicator = LED_ON;
        }

        prev_voltage_mv = voltage_mv;
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
    }
}

// ==================== MAIN ====================
void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Belt Monitor - GPIO16 ADC");
    ESP_LOGI(TAG, "  Platform: ESP32-S3");
    ESP_LOGI(TAG, "========================================");

    configure_led();
    initialize_adc();
    uart_init();

    float test_voltage = read_voltage_adc_mv();
    ESP_LOGI(TAG, "Initial voltage: %.2f mV", test_voltage);

    xTaskCreate(EventIndicatorTask, "LED", 4096, NULL, 3, NULL);
    xTaskCreate(belt_monitor_task, "BELT", 8192, NULL, 8, NULL);

    ESP_LOGI(TAG, "✅ System started");
}
