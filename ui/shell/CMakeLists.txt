idf_component_register(
    SRCS "ui_shell.c"
    INCLUDE_DIRS "include" "../../boards/common/include"
    REQUIRES presentation drawer notification_tray input network power security wifi_setup settings widgets orb runtime_control display lvgl esp_timer
)
