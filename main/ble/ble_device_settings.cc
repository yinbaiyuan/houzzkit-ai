#include "ble_device_settings.h"

#include "esphome_device.h"
#include "esphome_voice_device.h"

ProtoParse& BLEDeviceSettings::AppendDeviceSettingsInfo(ProtoParse& protoParse)
{
    return protoParse
        .pushUint8(ESPHomeVoiceDevice::GetInstance().outputVolume())
        .pushUint8(ESPHomeVoiceDevice::GetInstance().micEnabled() ? 1 : 0)
        .pushUint8(ESPHomeVoiceDevice::GetInstance().continuousDialogue() ? 1 : 0)
        .pushUint8(ESPHomeVoiceDevice::GetInstance().voiceResponseSound() ? 1 : 0)
        .pushUint8(ESPHomeDevice::GetInstance().idleScreenOff() ? 1 : 0)
        .pushUint8(ESPHomeVoiceDevice::GetInstance().sleepMode() ? 1 : 0)
        .pushUint32(ESPHomeVoiceDevice::GetInstance().sleepModeTimeInterval());
}

bool BLEDeviceSettings::ApplyDeviceSetting(uint8_t settingType, ProtoParse& protoParse, const DeviceNameUpdater& deviceNameUpdater)
{
    switch (settingType)
    {
    case PROPERTY_MIC_ENABLED:
    {
        uint8_t micEnable = protoParse.popUint8();
        ESPHomeVoiceDevice::GetInstance().setMicEnable(micEnable == 1);
        return true;
    }
    case PROPERTY_VOLUME:
    {
        uint8_t volume = protoParse.popUint8();
        ESPHomeVoiceDevice::GetInstance().setOutputVolume(volume);
        return true;
    }
    case PROPERTY_CONTINUOUS_DIALOGUE:
    {
        uint8_t continuousDialogue = protoParse.popUint8();
        ESPHomeVoiceDevice::GetInstance().setContinuousDialogue(continuousDialogue == 1);
        return true;
    }
    case PROPERTY_VOICE_RESPONSE_SOUND:
    {
        uint8_t voiceResponseSound = protoParse.popUint8();
        ESPHomeVoiceDevice::GetInstance().setVoiceResponseSound(voiceResponseSound == 1);
        return true;
    }
    case PROPERTY_IDLE_SCREEN_OFF:
    {
        uint8_t idleScreenOff = protoParse.popUint8();
        ESPHomeDevice::GetInstance().setIdleScreenOff(idleScreenOff == 1);
        return true;
    }
    case PROPERTY_SLEEP_MODE:
    {
        uint8_t sleepMode = protoParse.popUint8();
        ESPHomeVoiceDevice::GetInstance().setSleepMode(sleepMode == 1);
        return true;
    }
    case PROPERTY_SLEEP_MODE_TIME_INTERVAL:
    {
        uint32_t sleepModeTimeInterval = protoParse.popUint32();
        ESPHomeVoiceDevice::GetInstance().setSleepModeTimeInterval(sleepModeTimeInterval);
        return true;
    }
    case PROPERTY_DEVICE_NAME:
    {
        std::string deviceId = protoParse.popString8();
        std::string deviceName = protoParse.popString8();
        return deviceNameUpdater ? deviceNameUpdater(deviceId, deviceName) : true;
    }
    default:
        return true;
    }
}
