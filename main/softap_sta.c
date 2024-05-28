#include "uart.h"
#include "non_volatile_storage.h"
#include "driver/gpio.h"
#include "wifiStAp.h"
#include "simple_ota_example.h"
#include "server_component.h"
#define led1 GPIO_NUM_2
#define led2 GPIO_NUM_18
// GPIO 4,5,16,17 are reserve for uart
uint8_t led1_state = 0;
uint8_t led2_state = 0;

dataLoggerConfig read_config_struct;
dataLoggerConfig write_config_struct;

int s_retry_num = 0;

/* FreeRTOS event group to signal when we are connected/disconnected */
EventGroupHandle_t s_wifi_event_group;

void configure_led(void)
{
    gpio_reset_pin(led1);
    gpio_reset_pin(led2);
    gpio_set_direction(led1, GPIO_MODE_OUTPUT);
    gpio_set_direction(led2, GPIO_MODE_OUTPUT);
}

void led_on_off(uint8_t led, uint8_t led_state)
{
    gpio_set_level(led, led_state);
}

void app_main(void)
{
    char *TAG = "main";
    configure_led();
    led_on_off(led1, 0);
    led_on_off(led2, 0);
    // non volatile init
    ESP_ERROR_CHECK(nvs_init());

    if (read_config_from_NVS(&read_config_struct) == 0)
    {
        ESP_LOGI(TAG, "Unable to read from flash");
        read_config_struct.ST_SSID = "sidd";             // Example
        read_config_struct.ST_PASSWORD = "123456789011"; // Example
        read_config_struct.delay = "1000";               ////Example
        read_config_struct.URL = "http://lims.data.highlandenergynig.com/api/collect-data-store";
    }

    // uart
    uart_init();
    uart_port_t uart1 = UART_NUM_1;
    uart_port_t uart2 = UART_NUM_2;
    xTaskCreate(rx_task, "uart_rx_task_1", 1024 * 8, &uart1, configMAX_PRIORITIES - 1, NULL);
    // xTaskCreate(tx_task, "uart_tx_task_1", 1024 * 2, &uart1, configMAX_PRIORITIES - 2, NULL);
    xTaskCreate(rx_task, "uart_rx_task_2", 1024 * 8, &uart2, configMAX_PRIORITIES - 1, NULL);
    // xTaskCreate(tx_task, "uart_tx_task_2", 1024 * 2, &uart2, configMAX_PRIORITIES - 2, NULL);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize event group */
    s_wifi_event_group = xEventGroupCreate();

    /* Register Event handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    /*Initialize WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    /* Initialize AP */
    ESP_LOGI("WiFi SoftAP", "ESP_WIFI_MODE_AP");
    esp_netif_t *esp_netif_ap = wifi_init_softap();

    /* Initialize STA */
    ESP_LOGI("WiFi Sta", "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = wifi_init_sta(&read_config_struct);

    /* Start WiFi */
    ESP_ERROR_CHECK(esp_wifi_start());
    printf("return to main \n");
    initi_web_page_buffer();
    setup_server();
    /*
     * Wait until either the connection is established (WIFI_CONNECTED_BIT) or
     * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
     * The bits are set by event_handler() (see above)
     */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned,
     * hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI("WiFi Sta", "connected to ap SSID:%s password:%s",
                 read_config_struct.ST_SSID, read_config_struct.ST_PASSWORD);
        gpio_set_level(led2, 1);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI("WiFi Sta", "Failed to connect to SSID:%s, password:%s",
                 read_config_struct.ST_SSID, read_config_struct.ST_PASSWORD);
        gpio_set_level(led2, 0);
    }
    else
    {
        ESP_LOGE("WiFi Sta", "UNEXPECTED EVENT");
        return;
    }

    /* Set sta as the default interface */
    esp_netif_set_default_netif(esp_netif_sta);

    /* Enable napt on the AP netif */
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK)
    {
        ESP_LOGE("WiFi Sta", "NAPT not enabled on the netif: %p", esp_netif_ap);
    }

    printf("--------------------------------now post data---------------------------");

    // ota

    ESP_LOGI(TAG, "OTA example app_main start");
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // get_sha256_of_partitions();

    // xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);
}
