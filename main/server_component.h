#pragma once


#include <stdio.h>
#include <stdlib.h>
#include <string.h> //Requires by memset
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <esp_http_server.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>
#include <cJSON.h>
#include <esp_http_client.h>
#define LED_PIN 2


extern esp_err_t send_json_response(httpd_req_t *req, const char *json_response);


extern esp_err_t led_off_handler(httpd_req_t *req);

extern void initi_web_page_buffer(void);


extern esp_err_t file_handler(httpd_req_t *req, const char *file_path, const char *content_type);

extern httpd_handle_t setup_server(void);

extern esp_err_t pushconfig_hndl(httpd_req_t *req);
extern esp_err_t pushconfig_hndl_option(httpd_req_t *req);
extern esp_err_t validatePasswdOptionAPI(httpd_req_t *req);
extern esp_err_t validatePasswd(httpd_req_t *req);
extern esp_err_t SetPasswd_option(httpd_req_t *req);
extern esp_err_t SetPasswd(httpd_req_t *req);
void post_rest_function(char *URL,char *json_string);
char *createJSON(char *device_type, char *Device_ID, char *Data);

// extern uint8_t yer,mnth,dte,hh.mm.ss;