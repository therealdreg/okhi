idf_component_register(SRCS "okhi.c"
                    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_system lwip nvs_flash
                                  app_update esp_app_format esp_partition
                                  esp_driver_gpio esp_driver_spi esp_driver_uart spiffs
                                  spi_flash
                    EMBED_FILES "index.html.gz"
                    INCLUDE_DIRS ".")

# firmware/com sits outside this project, where CONFIG_COMPILER_HIDE_PATHS_MACROS does not
# reach: the __FILE__ of every ESP_ERROR_CHECK in com_esp.h put the build machine's absolute
# path, user name included, into the image. Map the repository root to a neutral name.
get_filename_component(OKHI_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
target_compile_options(${COMPONENT_LIB} PRIVATE "-fmacro-prefix-map=${OKHI_REPO_ROOT}=okhi")

spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)