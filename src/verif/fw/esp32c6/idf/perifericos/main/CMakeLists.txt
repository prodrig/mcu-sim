idf_component_register(SRCS "perifericos.c" INCLUDE_DIRS "."
                       REQUIRES esp_driver_ledc esp_driver_rmt esp_driver_spi esp_driver_i2s esp_driver_i2c
                                esp_driver_tsens esp_adc driver mbedtls)
