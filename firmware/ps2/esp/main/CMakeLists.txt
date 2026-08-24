idf_component_register(SRCS "okhi.c"
                    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_system lwip nvs_flash
                                  app_update esp_app_format esp_partition
                                  esp_driver_gpio esp_driver_spi esp_driver_uart spiffs
                                  spi_flash
                    EMBED_FILES "index.html.gz"
                    INCLUDE_DIRS ".")

spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)