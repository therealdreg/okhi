idf_component_register(SRCS "okhi.c"
                    PRIV_REQUIRES esp_wifi nvs_flash driver spiffs
                    INCLUDE_DIRS ".")

spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)