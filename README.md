| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C6 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | -------- |

# Simple OTA example

idf_component_register(SRCS "wifi.c" "main.c" "simple_ota_example.c" "server_component.c" "non_volatile_storage.c" "uart.c" "wifiStAp.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
                    
This example is based on `esp_https_ota` component's APIs.

## Configuration

Refer README.md in the parent directory for setup details.

Example also supports binding to specific interface (either "Ethernet" or "WiFi Station"), which will allow firmware upgrade to happen through specific interface (in case multiple networking interfaces are enabled). Please see more on this through example configuration in `idf.py menuconfig -> Example Configuration -> Support firmware upgrade bind specificied interface->Choose OTA data bind interface`.
