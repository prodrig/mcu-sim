idf_component_register(SRCS "motor.c" INCLUDE_DIRS "."
                       REQUIRES esp_driver_mcpwm esp_driver_pcnt esp_driver_gpio esp_hw_support)
