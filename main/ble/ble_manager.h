#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#if CONFIG_BT_ENABLED
#include <NimBLEDevice.h>
#include "ble_device_settings.h"

class BLEManager : public NimBLEServerCallbacks , NimBLECharacteristicCallbacks , ProtoParseCallbacks
{
private:

    ProtoParse _protoParse;

    typedef bool (BLEManager::*protoCallback)(const uint8_t*,uint16_t);

    std::map<uint8_t, std::function<bool(const uint8_t*,uint16_t)>> _protoCallbackMap;

    esp_timer_handle_t pushApTimer_ = nullptr;

    std::string md5(const std::string& str);

    std::string hashAuthorization(const std::string& url, const std::map<std::string, std::string>& params, const std::string& mac, const std::string& salt);

    std::string request(const std::string& method, const std::string& url, int16_t* status_code, const std::map<std::string, std::string>& headers = {}, const std::map<std::string, std::string>& payload = {});

    bool r_postDeviceName(const std::string& deviceId, const std::string& deviceName);

protected:
    
    NimBLEServer* bleServer = nullptr;

    NimBLEService* bleService = nullptr;

    NimBLECharacteristic *bleTxCharacteristic = nullptr;

    NimBLECharacteristic *bleRxCharacteristic = nullptr;

    NimBLECharacteristic *bleVersionCharacteristic = nullptr;

public:
    BLEManager()
    {
        _protoParse.setCallbacks(this);
        registerProto();
    };
    ~BLEManager(){};

    static BLEManager& GetInstance()
    {
        static BLEManager instance;
        return instance;
    }

    void protoCheck();

    void onRecvWifiConfig(std::function<bool(const std::string& ssid, const std::string& password)> callback);

    void start(const std::string& deviceName, bool enableConfigService = false);

    void stop();

    void free();

    void sendData(const uint8_t* data, size_t length);

    void recvData(const std::string& data);

    void setConfiguringWifi(bool eraseToken = false) { _protoParse.setConfiguringWifi(eraseToken); }

    // NimBLEServerCallbacks 
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override ;

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override ;

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override ;

    // NimBLECharacteristicCallbacks
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo);

    // ProtoParseCallbacks
    void onProtoParseComplete(uint8_t cmd,const uint8_t *payload, uint16_t length) override;

    void onProtoParseSend(uint8_t cmd, const uint8_t *payload, uint16_t length) override;

    bool otaStart(const std::string& firmware_url, const std::string& version);

    void otaProgress(uint8_t progress, uint16_t recent_read);

    void registerProto();

    void notifyMicSwitchState(bool state);

    void notifyVolume(uint8_t volume);

    void notifyContinuousDialogue(bool state);

    void notifyVoiceResponseSound(bool state);

    void notifyIdleScreenOff(bool state);

    void notifySleepMode(bool state);

    void notifySleepModeTimeInterval(uint32_t timeInterval);

    void pushAccessPoints();

    void stopPushAccessPoints();

};

#else

class BLEManager
{
public:
    BLEManager() = default;
    ~BLEManager() = default;

    static BLEManager& GetInstance();

    void protoCheck();

    void onRecvWifiConfig(std::function<bool(const std::string& ssid, const std::string& password)> callback);

    void start(const std::string& deviceName, bool enableConfigService = false);

    void stop();

    void free();

    void sendData(const uint8_t* data, size_t length);

    void recvData(const std::string& data);

    void setConfiguringWifi(bool eraseToken = false);

    bool otaStart(const std::string& firmware_url, const std::string& version);

    void otaProgress(uint8_t progress, uint16_t recent_read);

    void registerProto();

    void notifyMicSwitchState(bool state);

    void notifyVolume(uint8_t volume);

    void notifyContinuousDialogue(bool state);

    void notifyVoiceResponseSound(bool state);

    void notifyIdleScreenOff(bool state);

    void notifySleepMode(bool state);

    void notifySleepModeTimeInterval(uint32_t timeInterval);

    void pushAccessPoints();

    void stopPushAccessPoints();
};

#endif
