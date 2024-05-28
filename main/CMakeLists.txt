# Embed the server root certificate into the final binary
idf_build_get_property(project_dir PROJECT_DIR)




idf_component_register(SRCS "softap_sta.c" "simple_ota_example.c" "server_component.c" "non_volatile_storage.c" "uart.c" "wifiStAp.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-Wall -Wextra -Werror")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")



spiffs_create_partition_image(storage ../dtlgr_data FLASH_IN_PROJECT)

