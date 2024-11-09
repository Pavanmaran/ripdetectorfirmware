#include <string.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "wifiStAp.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY 6

// Wi-Fi Event Bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

EventGroupHandle_t s_wifi_event_group;
int s_retry_num = 0;
bool wifi_connected;
// Define tag
static const char *TAG = "WiFiStAp";

void wifi_init_softap(void);
// Wi-Fi monitoring task
void wifi_status_task(void *pvParameter) {
    while (1) {
        if (wifi_connected) {
            ESP_LOGI("Wi-Fi Status", "Connected to Wi-Fi network.");
            // Turn on the LED to indicate connected
            gpio_set_level(GPIO_NUM_4, 0);  // LED on
        } else {
            ESP_LOGI("Wi-Fi Status", "Disconnected from Wi-Fi network.");
            // Turn off the LED to indicate disconnected
            gpio_set_level(GPIO_NUM_4, 1);  // LED off
            // reset wifi
            esp_wifi_stop();
            esp_wifi_start();
            esp_wifi_connect();
        
        }
        vTaskDelay(pdMS_TO_TICKS(20000));  // Check every second
    }
}
// Wi-Fi event handler
void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_CONNECTED) {
            wifi_connected = true;
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_connected = false;
            esp_wifi_connect();
        }
    }
}
// Event handler for Wi-Fi events
static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_START)
        {
            esp_wifi_connect();
        }
        else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "Retrying to connect to the AP, attempt %d", s_retry_num);
                // Blink LED to indicate retrying
                gpio_set_level(GPIO_NUM_4, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(GPIO_NUM_4, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            else
            {
                ESP_LOGI(TAG, "Failed to connect to SSID after %d attempts. Switching to AP mode.", 
                EXAMPLE_ESP_MAXIMUM_RETRY);
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0; // Reset retry counter
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        gpio_set_level(GPIO_NUM_4, 0);  // LED ON

    }
}

// Initialize SoftAP mode as fallback
void wifi_init_softap(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = "IOT_Gateway",
            .ssid_len = strlen("IOT_Gateway"),
            .channel = 1,
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    if (strlen((char *)wifi_ap_config.ap.password) == 0)
    {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WiFi SoftAP", "SoftAP initialized. SSID:%s, Password:%s", "IOT_Gateway", "12345678");
}

void initialize_wifi(dataLoggerConfig *config)
{
    ESP_LOGI(TAG, "SSID: %s", config->ST_SSID);
    ESP_LOGI(TAG, "Password: %s", config->ST_PASSWORD);

    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        sta_netif,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        sta_netif,
                                                        &instance_got_ip));

    // Configure Wi-Fi station
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0},  // Initialize the array to avoid garbage values
            .password = {0},  // Initialize the array to avoid garbage values
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    strncpy((char *)wifi_config.sta.ssid, config->ST_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, config->ST_PASSWORD, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
       // Register the Wi-Fi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    ESP_LOGI(TAG, "WiFi initialized, trying to connect to SSID:%s", config->ST_SSID);
    
    // Wait for connection event
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);
    
    ESP_LOGI(TAG, "Event bits received: %d", bits);  // Debugging log

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to SSID:%s", config->ST_SSID);
        
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s. Switching to SoftAP mode.", config->ST_SSID);
        wifi_init_softap(); // Fallback to SoftAP mode
    }
    else
    {
        ESP_LOGE(TAG, "Unexpected event");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    xTaskCreate(wifi_status_task, "wifi_status_task", 2048, NULL, 5, NULL);
    vEventGroupDelete(s_wifi_event_group);
}
