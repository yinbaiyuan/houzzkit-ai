#pragma once

#include "esphome/core/macros.h"
#define ESPHOME_BOARD "HOUZZkit-Speaker"
#define ESPHOME_VARIANT "ESP32-S3"
#define USE_API
#define USE_API_NOISE
#define USE_ESP_IDF_VERSION_CODE VERSION_CODE(5, 4, 1)
#define USE_BUTTON
#define USE_SWITCH
#define USE_NUMBER
#define USE_MDNS
#define USE_NETWORK
#define USE_NETWORK_IPV6 false
#define USE_SOCKET_IMPL_BSD_SOCKETS
#define USE_SOCKET_SELECT_SUPPORT
#define USE_TEXT
#define USE_WIFI