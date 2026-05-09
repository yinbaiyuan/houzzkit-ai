#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "proto_parse.h"

enum BLE_DEVICE_PROPERTY : uint8_t {
    PROPERTY_MIC_ENABLED = 0,
    PROPERTY_VOLUME = 1,
    PROPERTY_CONTINUOUS_DIALOGUE = 2,
    PROPERTY_VOICE_RESPONSE_SOUND = 3,
    PROPERTY_IDLE_SCREEN_OFF = 4,
    PROPERTY_SLEEP_MODE = 5,
    PROPERTY_SLEEP_MODE_TIME_INTERVAL = 6,
    PROPERTY_DEVICE_NAME = 100,
};

class BLEDeviceSettings
{
public:
    using DeviceNameUpdater = std::function<bool(const std::string& deviceId, const std::string& deviceName)>;

    static ProtoParse& AppendDeviceSettingsInfo(ProtoParse& protoParse);
    static bool ApplyDeviceSetting(uint8_t settingType, ProtoParse& protoParse, const DeviceNameUpdater& deviceNameUpdater);
};
