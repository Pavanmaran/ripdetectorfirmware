#ifndef WIFI_ST_AP_H
#define WIFI_ST_AP_H
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_net_stack.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "driver/gpio.h"
#include <stdio.h>
#include "esp_http_client.h"
#include "my_data.h"
#include "server_component.h"
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "time.h"
#include "sys/time.h"
#include <stdlib.h>
#include "esp_check.h"
#include "non_volatile_storage.h"
#include "non_volatile_storage.h"
#include <string.h>
#include "uart.h"
#if IP_NAPT
#include "lwip/lwip_napt.h"
#endif
#include "lwip/err.h"
#include "lwip/sys.h"
#include "server_component.h"
#define BELL_PIN GPIO_NUM_2 /



/* AP Configuration */
#define EXAMPLE_ESP_WIFI_AP_SSID            "ESP DATALOGGER"
#define EXAMPLE_ESP_WIFI_AP_PASSWD          "12345678"
#define EXAMPLE_ESP_WIFI_CHANNEL            1
#define EXAMPLE_MAX_STA_CONN                3


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1



/* FreeRTOS event group to signal when we are connected/disconnected */
extern EventGroupHandle_t s_wifi_event_group;
extern int s_retry_num;








void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);



esp_netif_t *wifi_init_softap(void);

esp_netif_t *wifi_init_sta(dataLoggerConfig *NvsConfig);


uint8_t* char_to_uint8(const char *str);


#endif // WIFI_ST_AP_H