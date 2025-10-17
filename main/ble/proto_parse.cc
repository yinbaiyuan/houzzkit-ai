#include "proto_parse.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include "settings.h"
#include "esp_log.h"

#define TAG "ProtoParse"

void ProtoParse::invertUint16(uint16_t *dBuf, uint16_t *srcBuf)
{
    uint16_t tmp[4] = {0};
    for (uint8_t i = 0; i < 16; i++)
    {
        if (srcBuf[0] & (1 << i))
            tmp[0] |= 1 << (15 - i);
    }
    dBuf[0] = tmp[0];
}

uint16_t ProtoParse::CRC16_MODBUS(uint8_t *data, uint16_t datalen)
{
    uint16_t wCRCin = 0xFFFF;
    uint16_t wCPoly = 0x8005;
    invertUint16(&wCPoly, &wCPoly);
    while (datalen--)
    {
        wCRCin ^= *(data++);
        for (uint8_t i = 0; i < 8; i++)
        {
            if (wCRCin & 0x01)
                wCRCin = (wCRCin >> 1) ^ wCPoly;
            else
                wCRCin = wCRCin >> 1;
        }
    }
    uint16_t wCRCinTemp = wCRCin;
    return (wCRCin >> 8 & 0x00FF) | (wCRCinTemp << 8 & 0xFF00);
}

bool ProtoParse::updateClientToken(const std::string &token)
{
    if (token.empty() || token.size() != 32)
    {
        return false;
    }
    Settings settings("ble", true);
    _clientToken = token;
    settings.SetString("token", _clientToken);
    return true;
}

void ProtoParse::clearToken(bool erase)
{
    _clientToken.clear();
    if (erase)
    {
        Settings settings("ble", true);
        settings.SetString("token", _clientToken);
    }
}

ProtoParse::ProtoParse(size_t capacity) : ByteProtocol(capacity)
{
    Settings settings("ble");
    _clientToken = settings.GetString("token");
}

ProtoParse &ProtoParse::protoBegin(uint8_t cmd)
{
    enBegin();
    pushUint8(0);
    pushUint8(0);
    pushUint8(cmd);
    return *this;
}

void ProtoParse::parse(const uint8_t *data, const uint32_t length)
{
    _decodeVector.insert(_decodeVector.end(), data, data + length);
    if (_decodeVector.size() < 2)
    {
        return; // 等待后续
    }
    uint16_t protoDataLength = GetUint16(_decodeVector.data());
    if (protoDataLength + 2 > _decodeVector.size())
    {
        return; // 等待后续
    }
    if (this->CRC16_MODBUS((uint8_t *)_decodeVector.data() + 2, protoDataLength) == 0)
    {
        // CRC校验通过
        uint8_t cmd = _decodeVector.data()[2];
        if (cmd == 100) 
        {
            this->_protoParseCompletecallbacks->onProtoParseComplete(cmd, _decodeVector.data() + 3, protoDataLength - 3);
        }
        else
        {
            uint8_t tokenLength = _decodeVector.data()[3];
            if (tokenLength != 32)
            {
                protoBegin(cmd).pushUint8(2).protoSend();
            }
            else
            {
                std::string token((char *)(_decodeVector.data() + 4), tokenLength);
                if (token != _clientToken)
                {
                    ESP_LOGE(TAG, "ERROR!!! token FAILED!!!");
                    protoBegin(cmd).pushUint8(1).protoSend();
                }
                else
                {
                    if (this->_protoParseCompletecallbacks)
                    {
                        this->_protoParseCompletecallbacks->onProtoParseComplete(cmd, _decodeVector.data() + 4 + tokenLength, protoDataLength - 4 - tokenLength);
                    }
                }
            }
        }
    }
    _decodeVector.clear();
}

void ProtoParse::protoEnd()
{
    this->pushUint16(0);
    uint32_t size = _encodeVector.size();
    uint8_t* dataPointer = _encodeVector.data();
    uint16_t crc = this->CRC16_MODBUS(dataPointer + 2, size - 4);
    dataPointer[size - 2] = (crc >> 8) & 0xFF;
    dataPointer[size - 1] = (crc >> 0) & 0xFF;
    dataPointer[0] = ((size - 2) >> 8) & 0xFF;
    dataPointer[1] = ((size - 2) >> 0) & 0xFF;
}

#define BLE_PROTO_MAX_SIZE 20
void ProtoParse::protoSend()
{
    protoEnd();
    if (this->_protoParseCompletecallbacks)
    {
        uint32_t sendLength = BLE_PROTO_MAX_SIZE;
        uint32_t offset = 0;
        uint32_t length = _encodeVector.size();
        uint8_t* dataPointer = _encodeVector.data();
        while (length > 0)
        {
            sendLength = length > BLE_PROTO_MAX_SIZE ? BLE_PROTO_MAX_SIZE : length;
            length -= sendLength;
            this->_protoParseCompletecallbacks->onProtoParseSend(dataPointer[2], dataPointer + offset, sendLength);
            offset += sendLength;
        };
    }
}