#include "wifiStAp.h"



#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD   WIFI_AUTH_WAPI_PSK
#endif


// char *"WiFi SoftAP" = "WiFi SoftAP";
// char *"WiFi Sta" = "WiFi Sta";











void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI("WiFi SoftAP", "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI("WiFi SoftAP", "Station "MACSTR" left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI("WiFi Sta", "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI("WiFi Sta", "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Initialize soft AP */
esp_netif_t *wifi_init_softap(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_LOGI("WiFi SoftAP", "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_AP_SSID, EXAMPLE_ESP_WIFI_AP_PASSWD, EXAMPLE_ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}




esp_netif_t *wifi_init_sta(dataLoggerConfig *NvsConfig) {
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();
    int MAX_SSID_LENGTH=20;
    int MAX_PASSWORD_LENGTH=20;
    // Convert SSID and password strings to uint8_t arrays
    uint8_t ssid[MAX_SSID_LENGTH]; // Assuming MAX_SSID_LENGTH is the maximum length of SSID
    uint8_t password[MAX_PASSWORD_LENGTH]; // Assuming MAX_PASSWORD_LENGTH is the maximum length of password
    strncpy((char*)ssid, NvsConfig->ST_SSID, MAX_SSID_LENGTH - 1);
    strncpy((char*)password, NvsConfig->ST_PASSWORD, MAX_PASSWORD_LENGTH - 1);
    ssid[MAX_SSID_LENGTH - 1] = '\0'; // Ensure null-termination
    password[MAX_PASSWORD_LENGTH - 1] = '\0'; // Ensure null-termination

    // Configure the Wi-Fi settings
    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = 10,
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    strncpy((char*)wifi_sta_config.sta.ssid, (char*)ssid, sizeof(wifi_sta_config.sta.ssid) - 1);
    strncpy((char*)wifi_sta_config.sta.password, (char*)password, sizeof(wifi_sta_config.sta.password) - 1);

    // Set the Wi-Fi configuration
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    ESP_LOGI("WiFi Sta", "wifi_init_sta finished.");

    return esp_netif_sta;
}

uint8_t* char_to_uint8(const char *str) {
    size_t len = strlen(str);
    uint8_t *result = (uint8_t*)malloc(len + 1); // +1 for null terminator
    if (result == NULL) {
        // Error handling: unable to allocate memory
        return NULL;
    }
    strncpy((char*)result, str, len);
    result[len] = '\0'; // Ensure null-termination
    return result;
}













