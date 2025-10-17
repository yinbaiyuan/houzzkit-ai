//
//  ByteProtocol.hpp
//  SmarDoTSimulator
//
//  Created by Lawis on 2021/7/11.
//

#pragma once

#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <vector>

inline uint32_t GetUint32(const uint8_t *buffer)
{
    return (uint32_t(buffer[0]) << 24) + (uint32_t(buffer[1]) << 16) + (uint32_t(buffer[2]) << 8) + uint32_t(buffer[3]);
}

inline void SetUint32(uint8_t *buffer, uint32_t i)
{
    buffer[0] = i >> 24 & 0xFF;
    buffer[1] = i >> 16 & 0xFF;
    buffer[2] = i >> 8 & 0xFF;
    buffer[3] = i & 0xFF;
}

inline uint16_t GetUint16(const uint8_t *buffer)
{
    return (uint16_t(buffer[0]) << 8) + uint16_t(buffer[1]);
}

inline void SetUint16(uint8_t *buffer, uint16_t i)
{
    buffer[0] = i >> 8 & 0xFF;
    buffer[1] = i & 0xFF;
}

inline uint8_t GetUint8(const uint8_t *buffer)
{
    return buffer[0];
}

inline void SetUint8(uint8_t *buffer, uint8_t i)
{
    buffer[0] = i;
}

inline int8_t GetInt8(const uint8_t *buffer)
{
    return (int8_t)buffer[0];
}

inline void SetInt8(uint8_t *buffer, int8_t i)
{
    buffer[0] = i;
}

template <typename T>
class ByteProtocol
{
private:
    const uint8_t *_decodeBuffer = nullptr;

    uint32_t _decodeBytePointer = 0;

    uint32_t _decodeByteSize = 0;

    bool _decodeError = false;

    uint8_t _decodeByte = 0;

    uint8_t _decodeBitPointer = 7;

    uint8_t _encodeByte = 0;

    uint8_t _encodeBitPointer = 7;

protected:
    std::vector<uint8_t> _encodeVector;

private:
    void popBuffer(const uint8_t **buffer, uint32_t size)
    {
        if (_decodeBytePointer + size > _decodeByteSize)
        {
            _decodeError = true;
            *buffer = nullptr;
            return;
        }
        *buffer = (uint8_t *)(_decodeBuffer + _decodeBytePointer);
        _decodeBytePointer += size;
    };

    void setBit(uint8_t *b, uint8_t w)
    {
        (*b) |= (1 << w);
    };

    void clrBit(uint8_t *b, uint8_t w)
    {
        (*b) &= ~(1 << w);
    };

    void setBit(uint8_t *b, uint8_t w, bool v)
    {
        if (v)
        {
            setBit(b, w);
        }
        else
        {
            clrBit(b, w);
        }
    };

    bool getBit(uint8_t b, uint8_t w)
    {
        return (b >> w) & 0x01;
    };

public:
    ByteProtocol(size_t capacity) { _encodeVector.reserve(capacity); };

    ~ByteProtocol() {};

    T &enBegin()
    {
        _encodeVector.clear();
        _encodeVector.reserve(32);
        return static_cast<T &>(*this);
    };

    T &enByteBegin()
    {
        _encodeByte = 0;
        _encodeBitPointer = 7;
        return static_cast<T &>(*this);
    };

    T &pushBit(bool v)
    {
        setBit(&_encodeByte, _encodeBitPointer--, v);
        return static_cast<T &>(*this);
    };

    T &pushByte()
    {
        pushUint8(_encodeByte);
        return static_cast<T &>(*this);
    };

    T &pushUint8(uint8_t v)
    {
        _encodeVector.push_back(v);
        return static_cast<T &>(*this);
    };

    T &pushUint16(uint16_t v)
    {
        _encodeVector.push_back(v >> 8 & 0xFF);
        _encodeVector.push_back(v & 0xFF);
        return static_cast<T &>(*this);
    };

    T &pushUint24(uint32_t v)
    {
        _encodeVector.push_back(v >> 16 & 0xFF);
        _encodeVector.push_back(v >> 8 & 0xFF);
        _encodeVector.push_back(v & 0xFF);
        return static_cast<T &>(*this);
    };

    T &pushUint32(uint32_t v)
    {
        _encodeVector.push_back(v >> 24 & 0xFF);
        _encodeVector.push_back(v >> 16 & 0xFF);
        _encodeVector.push_back(v >> 8 & 0xFF);
        _encodeVector.push_back(v & 0xFF);
        return static_cast<T &>(*this);
    };

    T &pushInt8(int8_t v)
    {
        return pushUint8(v);
    };

    T &pushInt16(int16_t v)
    {
        return pushUint16(v);
    };

    T &pushInt32(int32_t v)
    {
        return pushUint32(v);
    };

    T &pushString8(const std::string &v)
    {
        uint8_t length = v.length();
        pushUint8(length);
        for (int16_t i = 0; i < length; i++)
        {
            _encodeVector.push_back(v.c_str()[i]);
        }
        return static_cast<T &>(*this);
    };

    T &pushString16(const std::string &v)
    {
        uint16_t length = v.length();
        pushUint16(length);
        const char *str = v.c_str();
        for (int32_t i = 0; i < length; i++)
        {
            _encodeVector.push_back(str[i]);
        }
        return static_cast<T &>(*this);
    };

    T &pushBuffer8(uint8_t *buffer, uint8_t size)
    {
        pushUint8(size);
        for (int16_t i = 0; i < size; i++)
        {
            _encodeVector.push_back(buffer[i]);
        }
        return static_cast<T &>(*this);
    };

    T &pushBuffer16(uint8_t *buffer, uint16_t size)
    {
        pushUint16(size);
        for (int32_t i = 0; i < size; i++)
        {
            _encodeVector.push_back(buffer[i]);
        }
        return static_cast<T &>(*this);
    };

    T &pushBuffer32(uint8_t *buffer, uint32_t size)
    {
        pushUint32(size);
        for (int32_t i = 0; i < size; i++)
        {
            _encodeVector.push_back(buffer[i]);
        }
        return static_cast<T &>(*this);
    };

    T &pushProtocol(T &proto)
    {
        uint32_t size = proto.getEncodeBufferSize();
        for (uint32_t i = 0; i < size; i++)
        {
            pushUint8(proto.getUint8(i));
        }
        return static_cast<T &>(*this);
    };

    void enEnd(std::vector<uint8_t> &protoVec)
    {
        protoVec = _encodeVector;
    };

    uint8_t getUint8(uint32_t index)
    {
        return _encodeVector.at(index);
    };

    uint32_t getEncodeBufferSize()
    {
        return (uint32_t)_encodeVector.size();
    };

    void deBegin(const uint8_t *buffer, uint32_t size)
    {
        _decodeBuffer = buffer;
        _decodeByteSize = size;
        _decodeBytePointer = 0;
        _decodeError = false;
    };

    uint8_t popUint8(uint8_t dv = 0)
    {
        if (_decodeBytePointer + 1 > _decodeByteSize)
        {
            return dv;
        }
        return _decodeBuffer[_decodeBytePointer++];
    };

    uint16_t popUint16(uint16_t dv = 0)
    {
        if (_decodeBytePointer + 2 > _decodeByteSize)
        {
            return dv;
        }
        uint16_t res = (uint16_t(_decodeBuffer[_decodeBytePointer]) << 8) + uint16_t(_decodeBuffer[_decodeBytePointer + 1]);
        _decodeBytePointer += 2;
        return res;
    };

    uint32_t popUint32(uint32_t dv = 0)
    {
        if (_decodeBytePointer + 4 > _decodeByteSize)
        {
            return dv;
        }
        uint32_t res = (uint32_t(_decodeBuffer[_decodeBytePointer]) << 24) + (uint32_t(_decodeBuffer[_decodeBytePointer + 1]) << 16) + (uint32_t(_decodeBuffer[_decodeBytePointer + 2]) << 8) + uint32_t(_decodeBuffer[_decodeBytePointer + 3]);
        _decodeBytePointer += 4;
        return res;
    };

    int8_t popInt8(int8_t dv = 0)
    {
        if (_decodeBytePointer + 1 > _decodeByteSize)
        {
            return dv;
        }
        return popUint8();
    };

    int16_t popInt16(int16_t dv = 0)
    {
        if (_decodeBytePointer + 2 > _decodeByteSize)
        {
            return dv;
        }
        return popUint16();
    };

    int32_t popInt32(int32_t dv = 0)
    {
        if (_decodeBytePointer + 4 > _decodeByteSize)
        {
            return dv;
        }
        return popUint32();
    };

    std::string popString8(const std::string &dv = "")
    {
        if (_decodeBytePointer + 1 > _decodeByteSize)
        {
            return dv;
        }
        uint8_t length = popUint8();
        if (_decodeBytePointer + length > _decodeByteSize)
        {
            _decodeError = true;
            return dv;
        }
        std::string res((char *)(_decodeBuffer + _decodeBytePointer), length);
        _decodeBytePointer += length;
        return res;
    };

    std::string popString16(const std::string &dv = "")
    {
        if (_decodeBytePointer + 2 > _decodeByteSize)
        {
            return dv;
        }
        uint16_t length = popUint16();
        if (_decodeBytePointer + length > _decodeByteSize)
        {
            _decodeError = true;
            return dv;
        }
        std::string res((char *)(_decodeBuffer + _decodeBytePointer), length);
        _decodeBytePointer += length;
        return res;
    };

    void popBuffer8(const uint8_t **buffer, uint8_t *size, const uint8_t *db = nullptr, uint8_t ds = 0)
    {
        if (_decodeBytePointer + 1 > _decodeByteSize)
        {
            *size = ds;
            *buffer = db;
            return;
        }
        *size = popUint8();
        popBuffer(buffer, *size);
    };

    void popBuffer16(const uint8_t **buffer, uint16_t *size, const uint8_t *db = nullptr, uint8_t ds = 0)
    {
        if (_decodeBytePointer + 2 > _decodeByteSize)
        {
            *size = ds;
            *buffer = db;
            return;
        }
        *size = popUint16();
        popBuffer(buffer, *size);
    };

    void popBuffer32(const uint8_t **buffer, uint32_t *size, const uint8_t *db = nullptr, uint8_t ds = 0)
    {
        if (_decodeBytePointer + 4 > _decodeByteSize)
        {
            *size = ds;
            *buffer = db;
            return;
        }
        *size = popUint32();
        popBuffer(buffer, *size);
    };

    void popByte()
    {
        if (_decodeBytePointer + 1 > _decodeByteSize)
        {
            _decodeByte = 0;
            _decodeBitPointer = UINT8_MAX;
            return;
        }
        _decodeByte = popUint8();
        _decodeBitPointer = 7;
    };

    bool popBit(bool dv = false)
    {
        if (_decodeBitPointer == UINT8_MAX)
        {
            return dv;
        }
        return getBit(_decodeByte, _decodeBitPointer--);
    };

    bool decodeError() { return _decodeError; }

    uint32_t remainingDeBufferSize()
    {
        return _decodeByteSize - _decodeBytePointer;
    };

    std::vector<uint8_t> returnEncodeBuffer() { return _encodeVector; };
};
