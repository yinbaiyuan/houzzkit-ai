#pragma once

#include <cstdint>
#include <string>

#include "sleep_mode_time_interval.h"

class ESPHomeDevice;

class ESPHomeVoiceDevice
{
public:
    ESPHomeVoiceDevice();
    ~ESPHomeVoiceDevice();

    static ESPHomeVoiceDevice& GetInstance();

    void setupPreferences();
    void RegisterEntities(ESPHomeDevice& device);

    void setOutputVolume(uint8_t volume);
    void setMicEnable(bool enabled);
    void setContinuousDialogue(bool enabled);
    void setVoiceResponseSound(bool enabled);
    void setPlayVoiceText(const std::string& value);
    void setExecuteCommandText(const std::string& value);
    void setAskAndExecuteCommandText(const std::string& value);
    void setSleepMode(bool enabled);
    void setSleepModeTimeInterval(uint32_t timeInterval);
    void setSleepModeStartTime(uint8_t hour, uint8_t minute);
    void setSleepModeEndTime(uint8_t hour, uint8_t minute);
    void updateIsInSleepModeInterval();
    void updateOutputVolume();

    bool micEnabled() const;
    uint8_t outputVolume() const;
    bool continuousDialogue() const;
    bool voiceResponseSound() const;
    bool sleepMode() const;
    uint32_t sleepModeTimeInterval();

private:
    bool _micEnabled = true;
    uint8_t _outputVolume = 70;
    bool _continuousDialogue = true;
    bool _voiceResponseSound = false;
    bool _sleepMode = false;
    SleepModeTimeInterval _sleepModeTimeInterval;
    bool _isInSleepModeInterval = false;
};
