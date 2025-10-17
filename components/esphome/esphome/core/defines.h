#pragma once

#ifndef USE_ESP32_VARIANT_ESP32S3
#define USE_ESP32_VARIANT_ESP32S3
#endif

#ifndef USE_ESP32_FRAMEWORK_ESP_IDF
#define USE_ESP32_FRAMEWORK_ESP_IDF
#endif

#ifndef USE_ESP32
#define USE_ESP32 1
#endif

#ifndef USE_ESP_IDF
#define USE_ESP_IDF
#endif

#ifndef ESPHOME_LOG_LEVEL
#define ESPHOME_LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#endif

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
#define USE_WIFI

