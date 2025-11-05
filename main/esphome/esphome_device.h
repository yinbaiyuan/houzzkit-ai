#pragma once

#include <stdio.h>
#include <string>
#include "sleep_mode_time_interval.h"

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

        // 在 ESPHome 中可以设置的功能
        void setOutputVolume(uint8_t volume);

        void setMicEnable(bool enabled);

        void setContinuousDialogue(bool enabled);

        void setVoiceResponseSound(bool enabled);

        void setIdleScreenOff(bool enabled);

        void setPlayVoiceText(const std::string &value);

        void setExecuteCommandText(const std::string &value);

        void setAskAndExecuteCommandText(const std::string &value);

        void setSleepMode(bool enabled);

        void setSleepModeTimeInterval(uint32_t timeInterval);

        void updateIsInSleepModeInterval();

        bool micEnabled() const { return _micEnabled; } 
        
        uint8_t outputVolume() const { return _outputVolume; }

        bool continuousDialogue() const { return _continuousDialogue; }

        bool voiceResponseSound() const { return _voiceResponseSound; }

        bool idleScreenOff() const { return _idleScreenOff; }

        bool sleepMode() const { return _sleepMode; }

    private:

        void initProperties();

        bool _micEnabled = true;

        uint8_t _outputVolume = 70;

        bool _continuousDialogue = true;

        bool _voiceResponseSound = false;

        bool _idleScreenOff = false;

        bool _sleepMode = false;

        SleepModeTimeInterval _sleepModeTimeInterval;

        bool _isInSleepModeInterval = false;
};