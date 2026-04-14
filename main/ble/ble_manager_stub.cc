#include "ble_manager.h"

BLEManager& BLEManager::GetInstance()
{
    static BLEManager instance;
    return instance;
}

void BLEManager::protoCheck()
{
}

void BLEManager::onRecvWifiConfig(std::function<bool(const std::string&, const std::string&)> callback)
{
}

void BLEManager::start(const std::string& deviceName, bool enableConfigService)
{
}

void BLEManager::stop()
{
}

void BLEManager::free()
{
}

void BLEManager::sendData(const uint8_t* data, size_t length)
{
}

void BLEManager::recvData(const std::string& data)
{
}

void BLEManager::setConfiguringWifi(bool eraseToken)
{
}

bool BLEManager::otaStart(const std::string& firmware_url, const std::string& version)
{
    return false;
}

void BLEManager::otaProgress(uint8_t progress, uint16_t recent_read)
{
}

void BLEManager::registerProto()
{
}

void BLEManager::notifyMicSwitchState(bool state)
{
}

void BLEManager::notifyVolume(uint8_t volume)
{
}

void BLEManager::notifyContinuousDialogue(bool state)
{
}

void BLEManager::notifyVoiceResponseSound(bool state)
{
}

void BLEManager::notifyIdleScreenOff(bool state)
{
}

void BLEManager::notifySleepMode(bool state)
{
}

void BLEManager::notifySleepModeTimeInterval(uint32_t timeInterval)
{
}

void BLEManager::pushAccessPoints()
{
}

void BLEManager::stopPushAccessPoints()
{
}
