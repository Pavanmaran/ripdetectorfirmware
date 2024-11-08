#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "non_volatile_storage.h"

// Define event bits for Wi-Fi connection status
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// External variables for Wi-Fi configuration and event handling
extern EventGroupHandle_t s_wifi_event_group;
extern int s_retry_num;

// Function declarations for initializing Wi-Fi
void initialize_wifi(dataLoggerConfig *config);
#endif // WIFI_H
