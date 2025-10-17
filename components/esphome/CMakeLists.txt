# Collect all source files recursively
set(ESPHOME_SOURCES
    "esphome/core/application.cpp"
    "esphome/core/color.cpp"
    "esphome/core/component.cpp"
    "esphome/core/component_iterator.cpp"
    "esphome/core/controller.cpp"
    "esphome/core/entity_base.cpp"
    "esphome/core/helpers.cpp"
    "esphome/core/log.cpp"
    "esphome/core/ring_buffer.cpp"
    "esphome/core/scheduler.cpp"
    "esphome/core/string_ref.cpp"
    "esphome/core/time.cpp"
    "esphome/core/util.cpp"
    "esphome/components/api/api_connection.cpp"
    "esphome/components/api/api_frame_helper.cpp"
    "esphome/components/api/api_pb2.cpp"
    "esphome/components/api/api_pb2_service.cpp"
    "esphome/components/api/api_server.cpp"
    "esphome/components/api/list_entities.cpp"
    "esphome/components/api/proto.cpp"
    "esphome/components/api/subscribe_state.cpp"
    "esphome/components/api/user_services.cpp"
    "esphome/components/esp32/core.cpp"
    "esphome/components/esp32/gpio.cpp"
    "esphome/components/esp32/preferences.cpp"
    "esphome/components/network/util.cpp"
    "esphome/components/socket/bsd_sockets_impl.cpp"
    "esphome/components/socket/lwip_raw_tcp_impl.cpp"
    "esphome/components/socket/lwip_sockets_impl.cpp"
    "esphome/components/socket/socket.cpp"
    "esphome/components/wifi/wifi_component.cpp"
    "esphome/components/wifi/wifi_component_esp_idf.cpp"
    "esphome/components/button/button.cpp"
    "esphome/components/switch/switch.cpp"
    "esphome/components/number/number.cpp"
    "esphome/components/number/number_call.cpp"
    "esphome/components/number/number_traits.cpp"
)

# Collect all include directories
set(ESPHOME_INCLUDE_DIRS
    "."
    "esphome/core"
    "esphome/components"
    "esphome/components/api"
    "esphome/components/esp32"
    "esphome/components/network"
    "esphome/components/socket"
    "esphome/components/wifi"
    "esphome/components/button"
    "esphome/components/switch"
    "esphome/components/number"
)

# Register the component with ESP-IDF
idf_component_register(
    SRCS ${ESPHOME_SOURCES}
    INCLUDE_DIRS ${ESPHOME_INCLUDE_DIRS}
    PRIV_REQUIRES lwip esp_wifi noise-c wpa_supplicant nvs_flash efuse esp_driver_gpio esp_ringbuf esp_driver_uart esp_driver_usb_serial_jtag 78__esp-wifi-connect 
)

# 编译选项
target_compile_options(${COMPONENT_LIB} PRIVATE
    -DUSE_ESP32
    -DUSE_ESP32_FRAMEWORK_ESP_IDF
    -DUSE_ESP32_VARIANT_ESP32S3
    -DUSE_ESP_IDF
    -Wno-nonnull-compare
    -Wno-sign-compare
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -fno-exceptions
    # -DARDUINO_USB_CDC_ON_BOOT=1
)