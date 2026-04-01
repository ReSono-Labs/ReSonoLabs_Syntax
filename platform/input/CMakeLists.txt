idf_component_register(
    SRCS "input_platform.c"
    INCLUDE_DIRS "include" "../../boards/common/include" "../../boards/s3_1_85c/include"
    REQUIRES driver freertos esp_driver_gpio esp_lcd espressif__esp_lcd_touch espressif__esp_lcd_touch_cst816s s3_1_85c_support
)
