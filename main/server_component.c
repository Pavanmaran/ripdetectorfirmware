#include "server_component.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> //Requires by memset
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <esp_http_server.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>
#include <cJSON.h>
#include "nvs_flash.h"
#include "esp_spi_flash.h"
#include "esp_spiffs.h"
#include <esp_http_client.h>
#include "non_volatile_storage.h"

#define ATS_HTML_PATH "/storage/index.html"
#define JS_FILE_PATH "/storage/jquery-3.3.1.min.js"
#define SVG_FILE_PATH "/storage/undraw-contact.svg"
#define CSS_STYLE_FILE_PATH "/storage/style.css"
#include "esp_spi_flash.h"
#include "esp_efuse.h" // Include the header for esp_efuse_mac_get_default
#include <stdio.h>     // For snprintf function
#include <stdint.h>    // For uint8_t type
#include "esp_mac.h"

dataLoggerConfig write_config_struct;
// Function prototype
char *get_mac_address(void);
uint8_t mac_address[6]; // MAC address is 6 bytes long

// Get MAC address as string
char *get_mac_address(void) {
    esp_efuse_mac_get_default(mac_address);
    static char mac_address_str[18];
    snprintf(mac_address_str, sizeof(mac_address_str), "%02x:%02x:%02x:%02x:%02x:%02x", 
             mac_address[0], mac_address[1], mac_address[2], 
             mac_address[3], mac_address[4], mac_address[5]);
    printf("MAC address: %s\n", mac_address_str);
    return mac_address_str;
}

// HTTP handler to return MAC address in JSON format
esp_err_t get_mac_address_handler(httpd_req_t *req) {
    char *mac_str = get_mac_address();
    char json_response[50];
    snprintf(json_response, sizeof(json_response), "{\"MAC Address\": \"%s\"}", mac_str);
    return send_json_response(req, json_response);
}

// Function to convert a long to a string
char *LongToString(long value)
{
    static char buffer[20]; // Adjust the buffer size as needed
    snprintf(buffer, sizeof(buffer), "%ld", value);
    return buffer;
}

// Function to create JSON string from key-value pairs
char *createJSON(char *device_type, char *Device_ID, char *Data)
{
    // Check for NULL inputs
    if (device_type == NULL || Device_ID == NULL || Data == NULL)
    {
        printf("Error: NULL input detected.\n");
        return NULL;
    }

    // Calculate length of JSON string
    int len = strlen(device_type) + strlen(Device_ID) + strlen(Data) + 100; // Extra characters for JSON formatting

    // Allocate memory for JSON string
    char *json = (char *)malloc(len * sizeof(char));

    // Check if memory allocation was successful
    if (json == NULL)
    {
        printf("Error: Memory allocation failed.\n");
        return NULL;
    }

    // Create JSON string
    // int written = snprintf(json, len, "{\"device_type\":\"%s\",\"Device_ID\":\"%s\",\"Data\":\"%s\"}", device_type, Device_ID, Data);
    int written = snprintf(json, len, "{\"device_type\": \"%s\",\"device_name\":\"%s\",\"date_time\": \"2024-04-20 12:10:10\", \"params\":\"%s\"}", device_type, Device_ID, Data);
    // Check if snprintf failed
    if (written < 0 || written >= len)
    {
        printf("Error: Failed to write JSON string.\n");
        free(json); // Free allocated memory
        return NULL;
    }

    return json;
}

esp_err_t file_handler(httpd_req_t *req, const char *file_path, const char *content_type)
{
    FILE *file = fopen(file_path, "r");
    if (file == NULL)
    {
        ESP_LOGE("HTTP_SERVER", "Failed to open file: %s", file_path);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    httpd_resp_set_type(req, content_type);
    // httpd_resp_set_type(req, "text/plain");  // Change the content type accordingly
    httpd_resp_set_hdr(req, "Content-Length", LongToString(file_size));

    httpd_resp_set_hdr(req, "Transfer-Encoding", "chunked");

    char buffer[1024];
    size_t bytesRead;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, bytesRead) != ESP_OK)
        {
            ESP_LOGE("HTTP_SERVER", "Failed to send file content");
            fclose(file);
            return ESP_FAIL;
        }
    }
    httpd_resp_send_chunk(req, NULL, 0);

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}
esp_err_t js_handler(httpd_req_t *req)
{
    return file_handler(req, JS_FILE_PATH, "text/plain");
}

// Handler to serve the SVG file
esp_err_t svg_handler(httpd_req_t *req)
{
    return file_handler(req, SVG_FILE_PATH, "image/svg+xml");
}

// Handler to serve the CSS file
esp_err_t css_style_handler(httpd_req_t *req)
{
    printf("in cssss                                 iiiiii\n");
    return file_handler(req, CSS_STYLE_FILE_PATH, "text/css");
}

void initi_web_page_buffer(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/storage",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t result = (esp_vfs_spiffs_register(&conf));
    if (result != ESP_OK)
    {
        ESP_LOGE("SPIFFS", "Fail to init SPIFFS (%s)", esp_err_to_name(result));
        return;
    }
    size_t total = 0, used = 0;
    result = esp_spiffs_info(conf.partition_label, &total, &used);
    if (result != ESP_OK)
    {
        ESP_LOGE("SPIFF", "Fail to get partition info(%s)", esp_err_to_name(result));
    }
    else
    {
        ESP_LOGI("SPIFF", "Psrtition size: total: %d, used: %d", total, used);
    }
}
esp_err_t send_html_page(httpd_req_t *req)
{
    FILE *file = fopen(ATS_HTML_PATH, "r");
    if (file == NULL)
    {
        ESP_LOGE("HTTP_SERVER", "Failed to open HTML file");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char buffer[1024];
    size_t bytesRead;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        httpd_resp_send_chunk(req, buffer, bytesRead);
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

const char *TAG = "Server component";

esp_err_t send_json_response(httpd_req_t *req, const char *json_response)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
    // httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");

    httpd_resp_set_type(req, "application/json");

    int response = httpd_resp_send(req, json_response, HTTPD_RESP_USE_STRLEN);
    return response;
}

esp_err_t led_off_handler(httpd_req_t *req)
{
    gpio_set_level(LED_PIN, 0);

    return send_json_response(req, "{\"status\":\"success\",\"message\":\"LED is OFF\"}");
}

httpd_uri_t validate_passwd_OPTIONAPI = {
    .uri = "/password/validate",
    .method = HTTP_OPTIONS,
    .handler = validatePasswdOptionAPI,
    .user_ctx = NULL};
httpd_uri_t validate_passwd = {
    .uri = "/password/validate",
    .method = HTTP_POST,
    .handler = validatePasswd,
    .user_ctx = NULL};
httpd_uri_t uri_pushConfig_optin = {
    .uri = "/pushconfig",
    .method = HTTP_OPTIONS,
    .handler = pushconfig_hndl_option,
    .user_ctx = NULL};
httpd_uri_t uri_pushConfig = {
    .uri = "/pushconfig",
    .method = HTTP_POST,
    .handler = pushconfig_hndl,
    .user_ctx = NULL};

httpd_uri_t uri_get_resetABS = {
    .uri = "/reset",
    .method = HTTP_GET,
    .handler = led_off_handler,
    .user_ctx = NULL};

httpd_uri_t uri_get_html_page = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = send_html_page,
    .user_ctx = NULL};
httpd_uri_t js_uri = {
    .uri = "/js/jquery-3.3.1.min.js",
    .method = HTTP_GET,
    .handler = js_handler,
    .user_ctx = NULL};

httpd_uri_t svg_uri = {
    .uri = "/images/undraw-contact.svg",
    .method = HTTP_GET,
    .handler = svg_handler,
    .user_ctx = NULL};

httpd_uri_t css_style_uri = {
    .uri = "/css/style.css",
    .method = HTTP_GET,
    .handler = css_style_handler,
    .user_ctx = NULL};

// Register the MAC address handler in the server setup
httpd_uri_t uri_get_mac_address = {
    .uri = "/getmac",
    .method = HTTP_GET,
    .handler = get_mac_address_handler,
    .user_ctx = NULL};


httpd_handle_t setup_server(void)
{
    ESP_LOGI("setup_server", "inside setup server");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 40; // We can use max 40 API in this project ATS
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get_html_page);
        httpd_register_uri_handler(server, &js_uri);
        httpd_register_uri_handler(server, &svg_uri);
        httpd_register_uri_handler(server, &css_style_uri);

        httpd_register_uri_handler(server, &validate_passwd_OPTIONAPI);
        httpd_register_uri_handler(server, &validate_passwd);
        httpd_register_uri_handler(server, &uri_pushConfig_optin);
        httpd_register_uri_handler(server, &uri_pushConfig);
        httpd_register_uri_handler(server, &uri_get_resetABS);
        httpd_register_uri_handler(server, &uri_get_mac_address); // MAC address handler
    }

    return server;
}

esp_err_t validatePasswdOptionAPI(httpd_req_t *req)
{

    return send_json_response(req, "");
}

esp_err_t validatePasswd(httpd_req_t *req)
{
    char *buf = NULL; // Pointer to store dynamically allocated memory
    size_t len = 0;   // Length of the received JSON data

    // Read the content of the request
    while (1)
    {
        // Read one chunk at a time
        char chunk[64];
        int ret = httpd_req_recv(req, chunk, sizeof(chunk));

        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_408(req);
            }
            free(buf); // Free the allocated memory in case of an error
            return ESP_FAIL;
        }

        // Allocate or reallocate memory to store the JSON data
        char *new_buf = realloc(buf, len + ret + 1);
        if (new_buf == NULL)
        {
            ESP_LOGE(TAG, "Memory allocation failed");
            free(buf); // Free the allocated memory in case of an error
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        buf = new_buf;

        // Copy the chunk to the allocated memory
        memcpy(buf + len, chunk, ret);
        len += ret;

        // Null-terminate the data
        buf[len] = '\0';

        // Check if we've received the entire payload
        if (ret < sizeof(chunk))
        {
            break;
        }
    }

    ESP_LOGI(TAG, "Received JSON data for timers: %s", buf);

    // Parse the received JSON data
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON data");
        httpd_resp_send_500(req);
        free(buf); // Free the allocated memory
        return ESP_FAIL;
    }

    // Extract password from JSON
    cJSON *password_json = cJSON_GetObjectItem(root, "password");
    if (password_json == NULL)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password field missing in JSON");
        return ESP_FAIL;
    }

    if (!cJSON_IsNumber(password_json))
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid password format");
        return ESP_FAIL;
    }

    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "result", true);
    // cJSON_AddNumberToObject(response,"password",intArray[9]);
    char *response_str = cJSON_PrintUnformatted(response);
    send_json_response(req, response_str);
    cJSON_free(response_str);
    cJSON_Delete(response);
    free(buf);

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t pushconfig_hndl_option(httpd_req_t *req)
{

    return send_json_response(req, "");
    ;
}

// Function to get the MAC address
// Function implementation

// Example function that uses get_mac_address

esp_err_t pushconfig_hndl(httpd_req_t *req)
{
    char *buf = NULL; // Pointer to store dynamically allocated memory
    size_t len = 0;   // Length of the received JSON data

    // Read the content of the request
    while (1)
    {
        // Read one chunk at a time
        char chunk[64];
        int ret = httpd_req_recv(req, chunk, sizeof(chunk));

        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_408(req);
            }
            free(buf); // Free the allocated memory in case of an error
            return ESP_FAIL;
        }

        // Allocate or reallocate memory to store the JSON data
        char *new_buf = realloc(buf, len + ret + 1);
        if (new_buf == NULL)
        {
            ESP_LOGE(TAG, "Memory allocation failed");
            free(buf); // Free the allocated memory in case of an error
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        buf = new_buf;

        // Copy the chunk to the allocated memory
        memcpy(buf + len, chunk, ret);
        len += ret;

        // Null-terminate the data
        buf[len] = '\0';

        // Check if we've received the entire payload
        if (ret < sizeof(chunk))
        {
            break;
        }
    }

    ESP_LOGI(TAG, "Received JSON data for timers: %s", buf);

    // Parse the received JSON data
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON data");
        httpd_resp_send_500(req);
        free(buf); // Free the allocated memory
        return ESP_FAIL;
    }

    // Extract password from JSON
    cJSON *ST_SSID_json = cJSON_GetObjectItem(root, "ssid");
    cJSON *ST_PASSWORD_JSON = cJSON_GetObjectItem(root, "password");
    cJSON *DELAY_JSON = cJSON_GetObjectItem(root, "delay");
    cJSON *SERVER_URL_JSON = cJSON_GetObjectItem(root, "url");
    if (ST_SSID_json == NULL || ST_PASSWORD_JSON == NULL || DELAY_JSON == NULL || SERVER_URL_JSON == NULL)
    {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password field missing in JSON");
        return ESP_FAIL;
    }
    printf("SSID: %s\n", ST_SSID_json->valuestring);
    printf("Password: %s\n", ST_PASSWORD_JSON->valuestring);
    printf("Delay: %s\n", DELAY_JSON->valuestring);
    printf("Server URL: %s\n", SERVER_URL_JSON->valuestring);
    write_config_struct.ST_SSID = ST_SSID_json->valuestring;
    write_config_struct.ST_PASSWORD = ST_PASSWORD_JSON->valuestring;
    write_config_struct.delay = DELAY_JSON->valuestring;
    write_config_struct.URL = SERVER_URL_JSON->valuestring;

    if (write_config_in_NVS(&write_config_struct) == false)
    {
        ESP_LOGI(TAG, "Unable to write into the flash");
    }
    vTaskDelay(pdMS_TO_TICKS(10000));
    // int password = password_json->valueint;
    // printf("password%d",password);
    // write_int_array_to_nvs(intArray, ARRAY_SIZE_INT, "my_int_data");
    // Check if the password matches

    free(buf);

    cJSON_Delete(root);
    send_json_response(req, "{\"status\": \"success\"}");

    return ESP_OK;
}

esp_err_t client_event_post_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        printf("HTTP_EVENT_ON_DATA: %.*s\n", evt->data_len, (char *)evt->data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

void post_rest_function(char *URL, char *json_string)
{
    esp_http_client_config_t config_post = {
        // .url = "http://192.168.29.24:8000/sensordata/",
        .url = "http://lims.data.highlandenergynig.com/api/collect-data-store",
        // .url = URL,
        .method = HTTP_METHOD_POST,
        .cert_pem = NULL,
        .event_handler = client_event_post_handler};

    esp_http_client_handle_t client = esp_http_client_init(&config_post);
    printf("%s", json_string);

    // char *post_data = "{\"device_type\": \"DATA LOGGER\",\"device_name\":\"eleven\",\"date_time\": \"2024-04-23 12:10:10\", \"params\":\"Send Output value\"}";
    esp_http_client_set_post_field(client, json_string, strlen(json_string));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_http_client_perform(client);
    esp_http_client_cleanup(client);
}
