idf_component_register(
    SRCS "display_platform.c"
    INCLUDE_DIRS "include" "../../boards/common/include" "../../boards/s3_1_85c/include"
    REQUIRES driver esp_lcd esp_driver_ledc esp_driver_gpio espressif__esp_lcd_st77916 s3_1_85c_support
    PRIV_REQUIRES lvgl__lvgl
)
