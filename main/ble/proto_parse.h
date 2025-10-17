#pragma once

#include "byte_protocol.h"

enum BLE_PROTO_CMD : uint8_t {
    CMD_GET_DEVICE_INFO = 1,        // 获取设备信息
    CMD_PUSH_ACCESS_POINTS = 2,     // 推送WiFi接入点
    CMD_CONNECT_WIFI = 10,          // 连接WiFi
    CMD_CONFIG_WEBSOCKET = 12,      // 配置WebSocket
    CMD_CONFIG_HOME_ASSISTANT = 20, // 配置Home Assistant
    CMD_OTA_UPDATE = 21,            // OTA升级
    CMD_OTA_PROGRESS = 22,          // OTA进度
    CMD_DEVICE_SETTINGS = 30,       // 设备设置
    CMD_SET_DEVICE_PROPERTY = 31,   // 设置设备设置
    CMD_UPDATE_TOKEN = 100          // 更新Token
};


class ProtoParseCallbacks   
{

public:
    virtual void onProtoParseComplete(uint8_t cmd,const uint8_t *payload, uint16_t length) = 0;

    virtual void onProtoParseSend(uint8_t cmd,const uint8_t *payload, uint16_t length) = 0;
};

class ProtoParse : public ByteProtocol<ProtoParse>
{

private:

    bool _isConfiguratingWifi = false;

    std::string _clientToken;

    ProtoParseCallbacks* _protoParseCompletecallbacks = nullptr;

    std::vector<uint8_t> _decodeVector;

    uint8_t* _parseBuffer = nullptr;

    uint16_t _parseLength = 0;

    void clearToken(bool erase = false);

protected:

    void invertUint16(uint16_t *dBuf, uint16_t *srcBuf);

    uint16_t CRC16_MODBUS(uint8_t *data, uint16_t datalen);

    bool updateClientToken(const std::string& token);

    void setConfiguringWifi(bool eraseToken = false) { _isConfiguratingWifi = true; clearToken(eraseToken); }

    void setConfiguredWifi() { _isConfiguratingWifi = false; }

    bool isConfiguringWifi() { return _isConfiguratingWifi; }

    void protoEnd();

    friend class BLEManager;

public:

    ProtoParse(size_t capacity = 512);
    
    ~ProtoParse(){};

    ProtoParse& protoBegin(uint8_t cmd);

    void parse(const uint8_t* data, const uint32_t length);

    void protoSend();

    void setCallbacks(ProtoParseCallbacks *callbacks) {
        _protoParseCompletecallbacks = callbacks;
    }

};

