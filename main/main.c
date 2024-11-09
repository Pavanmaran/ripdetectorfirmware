#include "uart.h"
#include "non_volatile_storage.h"
#include "driver/gpio.h"
#include "wifiStAp.h"
#include "simple_ota_example.h"
#include "server_component.h"
#include "wifi.h"
#define yled GPIO_NUM_4
#define rled GPIO_NUM_5
#define gled GPIO_NUM_27

// LED states
uint8_t yled_state = 1;

dataLoggerConfig read_config_struct;

// Function to configure GPIO pins for LEDs
void configure_led(void)
{
    gpio_reset_pin(yled);
    gpio_reset_pin(rled);
    gpio_reset_pin(gled);
    gpio_set_direction(yled, GPIO_MODE_OUTPUT);
    gpio_set_direction(rled, GPIO_MODE_OUTPUT);
    gpio_set_direction(gled, GPIO_MODE_OUTPUT);
}

// Function to control LED state
void led_on_off(uint8_t led, uint8_t led_state)
{
    gpio_set_level(led, led_state);
}

// Function to initialize UART
void initialize_uart(void)
{
    uart_init();
    uart_port_t uart1 = UART_NUM_1;
    uart_port_t uart2 = UART_NUM_2;
    xTaskCreate(rx_task, "uart_rx_task_1", 1024 * 8, &uart1, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(rx_task, "uart_rx_task_2", 1024 * 8, &uart2, configMAX_PRIORITIES - 1, NULL);
}

// Function to initialize OTA
void initialize_ota(void)
{
    ESP_LOGI("OTA", "Initializing OTA...");
    // OTA initialization logic
    xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);
}

#include "nvs_flash.h"
#include "nvs.h"

// Function to initialize default configuration in NVS if not found
void initialize_nvs_defaults(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("namespace_1", NVS_READWRITE, &my_handle);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI("NVS", "Namespace not found. Initializing default values.");
        
        // Open the NVS handle for writing
        err = nvs_open("namespace_1", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            // Write default values
            nvs_set_str(my_handle, "ST_SSID", "default_ssid");
            nvs_set_str(my_handle, "ST_PASSWORD", "default_password");
            nvs_set_str(my_handle, "delay", "1000");
            nvs_set_str(my_handle, "URL", "http://lims:");
            nvs_commit(my_handle); // Commit to save changes
            ESP_LOGI("NVS", "Default values written to NVS.");
        }
    } else if (err == ESP_OK) {
        ESP_LOGI("NVS", "Namespace found, no need to initialize.");
    } else {
        ESP_LOGE("NVS", "Error opening NVS namespace: %s", esp_err_to_name(err));
    }

    nvs_close(my_handle); // Close the NVS handle after use
}

// Application main function
void app_main(void)
{
    char *TAG = "main";

    // Configure LEDs
    configure_led();
    led_on_off(yled, 1); // Indicate system startup
    gpio_set_level(GPIO_NUM_27, 1); // turn of green led

    // Initialize NVS
    esp_err_t ret = nvs_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS");
        return;
    }
    
    // check if nvs is already written
    initialize_nvs_defaults();

    // Define your configuration structure
    dataLoggerConfig config;
    // config.ST_SSID = "Me";
    // config.ST_PASSWORD = "76919716";
    // config.delay = "10";  // Example delay value
    // config.URL = "http://lims.data.highlandenergynig.com/api/collect-data-store";

    // // Write configuration to NVS
    // if (write_config_in_NVS(&config)) {
    //     ESP_LOGI(TAG, "Configuration saved successfully!");
    // } else {
    //     ESP_LOGE(TAG, "Failed to save configuration.");
    // }

    // Attempt to read the configuration from NVS
    if (read_config_from_NVS(&config)) {
        ESP_LOGI(TAG, "Configuration loaded: SSID=%s, PASSWORD=%s, DELAY=%s, URL=%s",
                 config.ST_SSID, config.ST_PASSWORD, config.delay, config.URL);
    } else {
        ESP_LOGE(TAG, "Failed to load configuration.");
    }

    // Initialize UART communication
    initialize_uart();
    ESP_LOGI(TAG, "UART initialized");

    // Initialize network interfaces and Wi-Fi
    // if (esp_netif_init() != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to initialize network interface");
    //     return; // Handle error appropriately
    // }
    // ESP_LOGI(TAG, "Network interfaces initialized");
    // if (esp_event_loop_create_default() != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to create event loop");
    //     return; // Handle error appropriately
    // }
    // dataLoggerConfig config;
    ESP_LOGI(TAG, "Initializing wifi");
    initialize_wifi(&config);
    ESP_LOGI(TAG, "Wifi initialized");
    
    ESP_LOGI(TAG, "Initialization complete");

    // Initialize OTA
    // initialize_ota();

    // Initialize and serve the web page
    initi_web_page_buffer();
    setup_server();

    // Post initialization logic
}
