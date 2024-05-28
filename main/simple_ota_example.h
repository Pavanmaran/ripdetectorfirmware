#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "protocol_examples_common.h"
#include "string.h"





esp_err_t _http_event_handler(esp_http_client_event_t *evt);


void simple_ota_example_task(void *pvParameter);



void print_sha256(const uint8_t *image_hash, const char *label);


void get_sha256_of_partitions(void);