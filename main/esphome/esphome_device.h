#pragma once

#include <cstdint>
#include <stdio.h>
#include <string>

class ESPHomeDevice
{
    public:
        ESPHomeDevice();
        ~ESPHomeDevice();

        static ESPHomeDevice& GetInstance();

        void setupPreferences();
        void setNoisePsk(const std::string noise_psk);
        void setup();
        void loop();

        void setIdleScreenOff(bool enabled);
        void setOtaUpgradeUrl(const std::string &value);
        void setLatestVersion(const std::string &value);
        void setOtaDownloadProgress(uint8_t progress);
        void updateDeviceIp();

        bool idleScreenOff() const { return _idleScreenOff; }

        const std::string& otaUpgradeUrl() const { return _otaUpgradeUrl; }
        const std::string& latestVersion() const { return _latestVersion; }

    private:
        bool _idleScreenOff = false;
        uint8_t _otaDownloadProgress = 0;
        std::string _otaUpgradeUrl;
        std::string _latestVersion;
        std::string _deviceIp;
};
