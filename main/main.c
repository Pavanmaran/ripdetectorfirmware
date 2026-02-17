#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

// INA260 includes
#include "ina260.h"
#include "i2cdev.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include "driver/usb_serial_jtag.h"
#include "driver/uart.h"
#else
#include "driver/uart.h"
#endif

// ==================== ENABLE/DISABLE ADC ====================
#define USE_ADC_FALLBACK 0  // Set to 1 to enable ADC as fallback, 0 to disable completely

#if USE_ADC_FALLBACK
#include "driver/adc.h"
#include "esp_adc_cal.h"
#endif

static const char *TAG = "BELT";

// ==================== I2C CONFIGURATION ====================
// #define I2C_MASTER_SCL_IO GPIO_NUM_22
// #define I2C_MASTER_SDA_IO GPIO_NUM_21

#define I2C_MASTER_SCL_IO GPIO_NUM_9
#define I2C_MASTER_SDA_IO GPIO_NUM_8
#define INA260_I2C_ADDR 0x40

static ina260_t ina260_dev;

// ==================== PLATFORM-SPECIFIC CONFIGURATION ====================

#ifdef CONFIG_IDF_TARGET_ESP32S3
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_11
    #define UART_PC_NUM UART_NUM_0
    #define TXD_PIN_PC UART_PIN_NO_CHANGE
    #define RXD_PIN_PC UART_PIN_NO_CHANGE
    #if USE_ADC_FALLBACK
        #define ADC_PIN ADC1_CHANNEL_3
        #define ADC_UNIT_NUM ADC_UNIT_1
        #define USE_ADC1 1
        #define VOLTAGE_MULTIPLIER 2.0
    #endif
#else
    #define RELAY_PIN GPIO_NUM_5
    #define LED_PIN GPIO_NUM_27
    #define UART_PC_NUM UART_NUM_2
    #define TXD_PIN_PC GPIO_NUM_18
    #define RXD_PIN_PC GPIO_NUM_19
    #if USE_ADC_FALLBACK
        #define ADC_PIN ADC2_CHANNEL_0
        #define ADC_UNIT_NUM ADC_UNIT_2
        #define USE_ADC1 0
        #define VOLTAGE_MULTIPLIER 2.0
    #endif
#endif

// Common Configuration
// ==================== COMMON CONFIGURATION ====================

// Reduced threshold for accurate INA260 readings
#define VOLTAGE_DROP_THRESHOLD_MV 100.0  // Reduced from 150mV to 100mV for better sensitivity

#define CHECK_INTERVAL_MS 500
#define SEND_INTERVAL_MS 500
#define RIP_THRESHOLD_FACTOR 1.5
#define DEFAULT_INTERVAL_MS 10000

#if USE_ADC_FALLBACK
    #define ADC_ATTEN ADC_ATTEN_DB_11
    #define ADC_WIDTH ADC_WIDTH_BIT_12
#endif

#define RELAY_ACTIVE_LOW 0
#define LED_ACTIVE_LOW 1
#define RX_BUF_SIZE 128

// ==================== GLOBAL VARIABLES ====================

#if USE_ADC_FALLBACK
static esp_adc_cal_characteristics_t adc_chars;
#endif

static TickType_t last_drop_time = 0;
static TickType_t prev_interval = DEFAULT_INTERVAL_MS / portTICK_PERIOD_MS;
static bool first_drop_detected = false;
static float prev_voltage_mv = 0.0;  // Store INA260 voltage in millivolts
static uint8_t belt_ripped = 0;
static TickType_t last_send_time = 0;

enum {
    LED_ON,
    LED_OFF,
    RIP_INDICATOR
} indicator = LED_ON;

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
    gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 1 : 0); // Relay OFF initially
    
    ESP_LOGI(TAG, "GPIO configured - LED: %d, Relay: %d", LED_PIN, RELAY_PIN);
}

// ==================== ADC CONFIGURATION (OPTIONAL) ====================

#if USE_ADC_FALLBACK
void initialize_adc(void) {
#if USE_ADC1
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_PIN, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_NUM, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
    ESP_LOGI(TAG, "ADC1 initialized (fallback enabled)");
#else
    adc2_config_channel_atten(ADC_PIN, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_NUM, ADC_ATTEN, ADC_WIDTH, 1100, &adc_chars);
    ESP_LOGI(TAG, "ADC2 initialized (fallback enabled)");
#endif
}

uint32_t read_voltage_adc(void) {
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
#endif

// ==================== INA260 VOLTAGE READING ====================

float read_voltage_ina260_mv(void) {
    float voltage_v = 0.0;
    
    if (ina260_get_bus_voltage(&ina260_dev, &voltage_v) == ESP_OK) {
        return voltage_v * 1000.0;  // Convert volts to millivolts
    } else {
        ESP_LOGE(TAG, "Failed to read INA260 voltage");
        return prev_voltage_mv;  // Return previous value on error
    }
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
    ESP_LOGI(TAG, "Using USB Serial JTAG for PC communication");
#else
    ESP_ERROR_CHECK(uart_driver_install(UART_PC_NUM, RX_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PC_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PC_NUM, TXD_PIN_PC, RXD_PIN_PC, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART2 initialized (TX: GPIO%d, RX: GPIO%d)", TXD_PIN_PC, RXD_PIN_PC);
    
    const char* test_msg = "UART2 Communication Started\n";
    uart_write_bytes(UART_PC_NUM, test_msg, strlen(test_msg));
#endif
}

void sendDatatoPC(const char *data) {
#ifdef CONFIG_IDF_TARGET_ESP32S3
    printf("%s", data);
#else
    const int len = strlen(data);
    uart_write_bytes(UART_PC_NUM, data, len);
#endif
}

void sendLog(const char *data) {
    ESP_LOGI(TAG, "%s", data);
    sendDatatoPC(data);
}

// ==================== LED INDICATOR TASK ====================

// ==================== LED INDICATOR TASK ====================

void EventIndicatorTask(void *pvParameters) {
    while (1) {
        if (indicator == LED_ON) {
            // Normal state - LED stays ON
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        } 
        else if (indicator == LED_OFF) {
            // Loop detected - single blink
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);  // OFF
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 0 : 1);  // ON
            vTaskDelay(pdMS_TO_TICKS(500));
            indicator = LED_ON;  // Return to normal
        } 
        else if (indicator == RIP_INDICATOR) {
            // Belt ripped - continuous fast blinking
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 1 : 0);  // OFF
            vTaskDelay(pdMS_TO_TICKS(150));
            gpio_set_level(LED_PIN, LED_ACTIVE_LOW ? 0 : 1);  // ON
            vTaskDelay(pdMS_TO_TICKS(150));
            // Keep looping until indicator changes
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==================== BELT MONITORING TASK (INA260-BASED) ====================

// ==================== BELT MONITORING TASK (INA260-BASED) ====================

void belt_monitor_task(void *pvParameters) {
    char uart_buf[128];
    TickType_t current_time, time_since_last_drop;
    bool rip_sent = false;

    vTaskDelay(pdMS_TO_TICKS(1000));  // Wait for INA260 stabilization
    
    float voltage_mv = read_voltage_ina260_mv();
    prev_voltage_mv = voltage_mv;
    
    ESP_LOGI(TAG, "Baseline: %.2f mV (INA260)", voltage_mv);

    while (1) {
        current_time = xTaskGetTickCount();
        voltage_mv = read_voltage_ina260_mv();
        float voltage_diff = prev_voltage_mv - voltage_mv;  // Positive = voltage drop
        
        bool should_send = (current_time - last_send_time) >= (SEND_INTERVAL_MS / portTICK_PERIOD_MS);
        
        ESP_LOGI(TAG, "V=%.2f mV, Diff=%.2f mV", voltage_mv, voltage_diff);
        
        // FIRST LOOP DETECTION
        if (!first_drop_detected && voltage_diff > VOLTAGE_DROP_THRESHOLD_MV) {
            first_drop_detected = true;
            last_drop_time = current_time;
            prev_voltage_mv = voltage_mv;
            rip_sent = false;
            belt_ripped = 0;
            
            snprintf(uart_buf, sizeof(uart_buf), "S:1,V:%.0f,I:0,T:0\n", voltage_mv);
            sendDatatoPC(uart_buf);
            ESP_LOGI(TAG, "✓ FIRST LOOP: %.2f mV (drop=%.2f)", voltage_mv, voltage_diff);
            
            // LED single blink for first detection
            indicator = LED_OFF;
            
            last_send_time = current_time;
        }
        // SUBSEQUENT LOOP DETECTION (belt OK)
        else if (first_drop_detected && voltage_diff > VOLTAGE_DROP_THRESHOLD_MV) {
            TickType_t interval = current_time - last_drop_time;
            
            if (interval > CHECK_INTERVAL_MS / portTICK_PERIOD_MS) {
                prev_interval = interval;
                last_drop_time = current_time;
                belt_ripped = 0;
                rip_sent = false;
                
                // Relay stays OFF (belt is OK - normal operation)
                gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 1 : 0);
                
                uint32_t interval_ms = interval * portTICK_PERIOD_MS;
                snprintf(uart_buf, sizeof(uart_buf), "S:2,V:%.0f,I:%lu,T:0\n", 
                         voltage_mv, (unsigned long)interval_ms);
                sendDatatoPC(uart_buf);
                ESP_LOGI(TAG, "✓ LOOP OK: I=%lu ms (Relay OFF)", (unsigned long)interval_ms);
                
                // LED single blink to indicate loop detected
                indicator = LED_OFF;
                
                last_send_time = current_time;
                prev_voltage_mv = voltage_mv;
            }
        }
        // RIP DETECTION (next loop NOT detected within threshold)
        else if (first_drop_detected) {
            time_since_last_drop = current_time - last_drop_time;
            
            if (time_since_last_drop > (TickType_t)(prev_interval * RIP_THRESHOLD_FACTOR)) {
                if (!rip_sent) {
                    belt_ripped = 1;
                    
                    // Turn ON relay (belt ripped - ALARM)
                    gpio_set_level(RELAY_PIN, RELAY_ACTIVE_LOW ? 0 : 1);
                    ESP_LOGE(TAG, "🚨 RELAY ON - BELT RIPPED ALARM!");
                    
                    uint32_t time_ms = time_since_last_drop * portTICK_PERIOD_MS;
                    snprintf(uart_buf, sizeof(uart_buf), "S:3,V:%.0f,I:0,T:%lu\n",
                             voltage_mv, (unsigned long)time_ms);
                    sendDatatoPC(uart_buf);
                    ESP_LOGE(TAG, "⚠️ BELT RIPPED! Time since last loop: %lu ms", (unsigned long)time_ms);
                    
                    rip_sent = true;
                    last_send_time = current_time;
                }
                
                // Keep sending rip status every interval
                if (should_send) {
                    uint32_t time_ms = time_since_last_drop * portTICK_PERIOD_MS;
                    snprintf(uart_buf, sizeof(uart_buf), "S:3,V:%.0f,I:0,T:%lu\n",
                             voltage_mv, (unsigned long)time_ms);
                    sendDatatoPC(uart_buf);
                    ESP_LOGE(TAG, "⚠️ STILL RIPPED - Time: %lu ms", (unsigned long)time_ms);
                    last_send_time = current_time;
                }
                
                // LED continuous fast blinking (RIP_INDICATOR)
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
        // WAITING FOR FIRST DETECTION
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

// ==================== I2C INITIALIZATION ====================

void initialize_i2c_ina260(void) {
    ESP_ERROR_CHECK(i2cdev_init());
    
    ESP_ERROR_CHECK(ina260_init_desc(&ina260_dev, INA260_I2C_ADDR, I2C_NUM_0, 
                                      I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));
    
    ESP_ERROR_CHECK(ina260_init(&ina260_dev));
    
    // Configure averaging & conversion time for stable readings
    ESP_ERROR_CHECK(ina260_set_config(&ina260_dev, 
                                       INA260_MODE_CONT_SHUNT_BUS,
                                       INA260_AVG_16,
                                       INA260_CT_1100,
                                       INA260_CT_1100));
    
    ESP_LOGI(TAG, "✅ INA260 initialized (SDA=%d, SCL=%d)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
}

// ==================== INA260 MONITORING TASK (DISPLAY ONLY) ====================

void ina260_monitor_task(void *pvParameters) {
    float voltage, current, power;
    char i2c_buf[128];
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    while (1) {
        if (ina260_get_bus_voltage(&ina260_dev, &voltage) == ESP_OK &&
            ina260_get_current(&ina260_dev, &current) == ESP_OK &&
            ina260_get_power(&ina260_dev, &power) == ESP_OK) {
            
            // ESP_LOGI(TAG, "INA260: V=%.3fV, I=%.3fA, P=%.3fW", voltage, current, power);
            
            // snprintf(i2c_buf, sizeof(i2c_buf), "INA:V=%.3f,I=%.3f,P=%.3f\n", 
            //          voltage, current, power);
            // sendDatatoPC(i2c_buf);
        } else {
            ESP_LOGE(TAG, "Failed to read INA260");
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));  // Read every 1 second for display
    }
}

// ==================== MAIN APPLICATION ====================

void app_main(void) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Belt Monitoring System v2.0 (INA260)");
#ifdef CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "  Platform: ESP32-S3");
#else
    ESP_LOGI(TAG, "  Platform: ESP32");
#endif
    ESP_LOGI(TAG, "  ADC Fallback: %s", USE_ADC_FALLBACK ? "ENABLED" : "DISABLED");
    ESP_LOGI(TAG, "========================================");

    configure_led();

#if USE_ADC_FALLBACK
    initialize_adc();
    ESP_LOGI(TAG, "ADC fallback initialized");
#else
    ESP_LOGI(TAG, "ADC disabled - using INA260 only");
#endif

    uart_init();
    initialize_i2c_ina260();

    // Test INA260 voltage reading
    float test_voltage = read_voltage_ina260_mv();
    char test_buf[100];
    snprintf(test_buf, sizeof(test_buf), "Initial INA260 voltage: %.2f mV", test_voltage);
    sendLog(test_buf);

    // Create tasks
    xTaskCreate(EventIndicatorTask, "EventIndicatorTask", 4096, NULL, 3, NULL);
    xTaskCreate(belt_monitor_task, "belt_monitor_task", 8192, NULL, 8, NULL);
    xTaskCreate(ina260_monitor_task, "ina260_monitor", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "✅ System started with INA260 voltage monitoring");
}
