idf_component_register(
    SRCS "power_platform.c"
    INCLUDE_DIRS "include" "../../boards/common/include"
    REQUIRES esp_hw_support esp_system display
)
