idf_component_register(
    SRCS "audio_platform.c"
    INCLUDE_DIRS "include" "../../boards/common/include" "../../boards/s3_1_85c/include" "../../boards/s3_1_85c_support/include"
    REQUIRES driver freertos esp_driver_i2s esp_timer esp_driver_gpio s3_1_85c_support
)
