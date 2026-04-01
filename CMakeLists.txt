cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    boards/s3_1_85c_support
    app
    core/state
    core/presentation
    core/runtime_control
    platform/display
    platform/input
    platform/audio
    platform/network
    platform/power
    platform/storage
    platform/web_server
    services/security
    services/wifi_setup
    services/wifi_provisioning
    services/orb
    services/ota
    services/settings
    ui/shell
    ui/drawer
    ui/notification_tray
    ui/widgets
    ui/web_common
    ui/web_shell
    third_party/compact25519
    third_party/espressif__esp_websocket_client
    runtime/provider/provider_storage
    runtime/provider/provider_session
    runtime/provider/provider_terminal
    runtime/provider/provider_transport
    runtime/provider/provider_state_bridge
    runtime/provider/provider_ui_bridge
    runtime/provider/provider_web_bridge
    runtime/provider/provider_runtime_api
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_device_runtime)

# Suppress GCC false-positive truncation warnings triggered by -O2.
# Both patterns are safe in our code (explicit null termination follows strncpy;
# snprintf output is bounded by the actual SSID max length).
add_compile_options(-Wno-stringop-truncation -Wno-format-truncation)
