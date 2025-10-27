# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)




idf_component_register(SRCS "wifi.c" "main.c" "uart.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES driver esp_adc esp_driver_usb_serial_jtag)
set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Werror")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")



