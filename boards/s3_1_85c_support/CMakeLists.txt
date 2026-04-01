idf_component_register(
    SRCS "s3_1_85c_support.c"
    INCLUDE_DIRS "include" "../s3_1_85c/include"
    REQUIRES driver freertos esp_driver_i2c
)
