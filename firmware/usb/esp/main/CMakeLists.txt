idf_component_register(SRCS "okhi.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_driver_gpio esp_driver_spi esp_driver_uart spiffs
                    INCLUDE_DIRS ".")

spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)