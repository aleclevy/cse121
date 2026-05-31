idf_component_register(
    SRCS ultrasonic.c
    INCLUDE_DIRS .
    REQUIRES driver freertos esp_timer esp_driver_gpio esp_rom esp_driver_i2c
)
