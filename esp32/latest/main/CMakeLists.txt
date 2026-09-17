set(KVM_SOURCES
    "app_main.c"
    "app_state.c"
    "button_service.c"
    "button_gesture.c"
    "cooler_service.c"
    "hardware.c"
    "kvm_service.c"
    "kvm_font_unscii_12.c"
    "lcd_lvgl.c"
    "network_manager.c"
    "protocol.c"
    "ui.c"
    "websocket_server.c"
)

idf_component_register(
    SRCS ${KVM_SOURCES}
    INCLUDE_DIRS "include"
    EMBED_FILES "assets/boot_apple.rgb565"
    REQUIRES
        esp_common
        esp_system
        heap
        spi_flash
        esp_driver_gpio
        esp_driver_spi
        esp_event
        esp_http_server
        esp_lcd
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
)

# The established private configuration lives at esp32/kvm_config.local.h.
# It remains outside this project and is never copied into a distributable tree.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../kvm_config.local.h")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KVM_HAS_LOCAL_CONFIG=1)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../..")
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KVM_HAS_LOCAL_CONFIG=0)
endif()
