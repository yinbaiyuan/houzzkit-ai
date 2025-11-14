#include "ble_manager.h"
#include "wifi_station.h"
#include "wifi_configuration_ap.h"
#include "board.h"
#include "application.h"
#include "system_info.h"
#include <esp_app_desc.h>
#include "esphome_device.h"
#include "settings.h"
#include "assets/lang_config.h"
#include <esp_wifi_types_generic.h>
#include <mbedtls/md5.h>
#include <random>
#include <chrono>

#define BLE_CONFIG_SERVICE_UUID "2F8A7C3C-9B6E-3A5F-8D2C-7E1B4F6A9C3D"
#define CHARACTERISTIC_UUID_CONFIG "2F8A7C3D-9B6E-3A5F-8D2C-7E1B4F6A9C3D"
#define BLE_UUID "2F8A7C4C-9B6E-3A5F-8D2C-7E1B4F6A9C3D"
#define CHARACTERISTIC_UUID_RX "2F8A7C4D-9B6E-3A5F-8D2C-7E1B4F6A9C3D"
#define CHARACTERISTIC_UUID_TX "2F8A7C4E-9B6E-3A5F-8D2C-7E1B4F6A9C3D"

#define HK_BLE_PROTO_VERSION_MAJOR 0x00
#define HK_BLE_PROTO_VERSION_MINOR 0x01
#define HK_BLE_PROTO_VERSION_PATCH 0x00

#define TAG "BLEManager"

void BLEManager::onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo)
{
    ESP_LOGI("NimBLEServer", "Client address: %s", connInfo.getAddress().toString().c_str());
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
    NimBLEDevice::startAdvertising();
}

void BLEManager::onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason)
{
    ESP_LOGI("NimBLEServer", "Client disconnected - start advertising");
    this->stopPushAccessPoints();
    NimBLEDevice::startAdvertising();
}

void BLEManager::onMTUChange(uint16_t MTU, NimBLEConnInfo &connInfo)
{
    ESP_LOGI("NimBLEServer", "MTU updated: %u for connection ID: %u", MTU, connInfo.getConnHandle());
}

void BLEManager::onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo)
{
    this->recvData(pCharacteristic->getValue());
}

// ProtoParseCallbacks
void BLEManager::onProtoParseComplete(uint8_t cmd, const uint8_t *payload, uint16_t length)
{
    if (_protoCallbackMap.find(cmd) != _protoCallbackMap.end())
    {
        _protoParse.deBegin(payload, length);
        if (!(_protoCallbackMap[cmd])(payload, length))
        {
            ESP_LOGE(TAG, "CMD: %d execute failed!", cmd);
        }
    }
    else
    {
        ESP_LOGE(TAG, "CMD: %d found failed!", cmd);
    }
}

void BLEManager::onProtoParseSend(uint8_t cmd, const uint8_t *payload, uint16_t length)
{
    this->sendData(payload, length);
}

void BLEManager::notifyMicSwitchState(bool state)
{
    _protoParse.protoBegin(CMD_SET_DEVICE_PROPERTY).pushUint8(PROPERTY_MIC_ENABLED).pushUint8(state).protoSend();
}

void BLEManager::notifyVolume(uint8_t volume)
{
    _protoParse.protoBegin(CMD_SET_DEVICE_PROPERTY).pushUint8(PROPERTY_VOLUME).pushUint8(volume).protoSend();
}


void BLEManager::notifyContinuousDialogue(bool state)
{
    _protoParse.protoBegin(CMD_SET_DEVICE_PROPERTY).pushUint8(PROPERTY_CONTINUOUS_DIALOGUE).pushUint8(state).protoSend();
}

void BLEManager::notifyVoiceResponseSound(bool state)
{
    _protoParse.protoBegin(CMD_SET_DEVICE_PROPERTY).pushUint8(PROPERTY_VOICE_RESPONSE_SOUND).pushUint8(state).protoSend();
}

void BLEManager::notifyIdleScreenOff(bool state)
{
    _protoParse.protoBegin(CMD_SET_DEVICE_PROPERTY).pushUint8(PROPERTY_IDLE_SCREEN_OFF).pushUint8(state).protoSend();
}

void BLEManager::notifySleepMode(bool state)
{
    _protoParse.protoBegin(CMD_SET_DEVICE_PROPERTY).pushUint8(PROPERTY_SLEEP_MODE).pushUint8(state).protoSend();
}

void BLEManager::notifySleepModeTimeInterval(uint32_t timeInterval)
{
    _protoParse.protoBegin(CMD_SET_DEVICE_PROPERTY).pushUint8(PROPERTY_SLEEP_MODE_TIME_INTERVAL).pushUint32(timeInterval).protoSend();
}

bool BLEManager::otaStart(const std::string &firmware_url, const std::string &version)
{
    Application::GetInstance().startOtaUpgrade(firmware_url, version);
    return true;
}

void BLEManager::otaProgress(uint8_t progress, uint16_t recent_read)
{
    _protoParse.protoBegin(CMD_OTA_PROGRESS).pushUint8(progress).pushUint16(recent_read).protoSend();
}

void BLEManager::start(const std::string &deviceName, bool enableConfigService)
{
    if (bleServer == nullptr)
    {
        NimBLEDevice::init(deviceName);

        bleServer = NimBLEDevice::createServer();
        bleServer->setCallbacks(this);

        bleService = bleServer->createService(BLE_UUID);

        bleTxCharacteristic = bleService->createCharacteristic(
            CHARACTERISTIC_UUID_TX,
            NIMBLE_PROPERTY::NOTIFY);

        bleRxCharacteristic = bleService->createCharacteristic(
            CHARACTERISTIC_UUID_RX,
            NIMBLE_PROPERTY::WRITE);
        bleRxCharacteristic->setCallbacks(this);

        BLEAdvertisementData oAdvertisementCustom = BLEAdvertisementData();
        oAdvertisementCustom.setName(deviceName);
        uint8_t ble_version_data[] = {0x09, 0xFF, 'h', 'z', 'k', 't', 'd', HK_BLE_PROTO_VERSION_MAJOR, HK_BLE_PROTO_VERSION_MINOR, HK_BLE_PROTO_VERSION_PATCH};
        oAdvertisementCustom.addData(ble_version_data, sizeof(ble_version_data));

        NimBLEDevice::getAdvertising()->setAdvertisementData(oAdvertisementCustom);
        NimBLEDevice::getAdvertising()->enableScanResponse(true);
    }
    if (enableConfigService)
    {
        NimBLEDevice::getAdvertising()->addServiceUUID(BLE_CONFIG_SERVICE_UUID);
    }
    else
    {
        NimBLEDevice::getAdvertising()->removeServiceUUID(BLE_CONFIG_SERVICE_UUID);
    }
    bleService->start();
    NimBLEDevice::getAdvertising()->start();
}

void BLEManager::stop()
{
    if (bleServer != nullptr)
    {
        bleServer->getAdvertising()->stop();
    }
}

void BLEManager::free()
{
    if (bleServer != nullptr)
    {
        bleServer->getAdvertising()->stop();
        bleServer->removeService(bleService);
        NimBLEDevice::deinit(true);
        bleServer = nullptr;
        bleService = nullptr;
        bleTxCharacteristic = nullptr;
        bleRxCharacteristic = nullptr;
        bleVersionCharacteristic = nullptr;
    }
}

void BLEManager::sendData(const uint8_t *data, size_t length)
{
    if (bleTxCharacteristic)
    {
        bleTxCharacteristic->notify(data, length);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void BLEManager::recvData(const std::string &data)
{
    _protoParse.parse((const uint8_t *)data.c_str(), data.length());
}

void BLEManager::pushAccessPoints()
{
    if (pushApTimer_)
        return;
    esp_timer_create_args_t timer_args = {
        .callback = [](void *arg)
        {
            auto *self = static_cast<BLEManager *>(arg);
            auto &wifi_ap = WifiConfigurationAp::GetInstance();
            auto ap_records = wifi_ap.GetAccessPoints();
            self->_protoParse.protoBegin(3).pushUint8(ap_records.size());
            for (auto &ap_record : ap_records)
            {
                self->_protoParse.pushString8(std::string((const char *)ap_record.ssid)).pushInt8(ap_record.rssi).pushUint8(ap_record.authmode);
                ESP_LOGI(TAG, "SSID: %s, RSSI: %d, AuthMode: %d", ap_record.ssid, ap_record.rssi, ap_record.authmode);
            }
            self->_protoParse.protoSend();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "push_ap_timer",
        .skip_unhandled_events = true};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &pushApTimer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(pushApTimer_, 5000000));
}

void BLEManager::stopPushAccessPoints()
{
    if (pushApTimer_)
    {
        esp_timer_stop(pushApTimer_);
        esp_timer_delete(pushApTimer_);
        pushApTimer_ = nullptr;
    }
}

std::string BLEManager::md5(const std::string &str)
{
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts(&ctx);
    mbedtls_md5_update(&ctx, (const unsigned char *)str.c_str(), str.length());
    uint8_t output[16];
    mbedtls_md5_finish(&ctx, output);
    char md5_str[33] = {0};
    for (int i = 0; i < 16; i++)
    {
        snprintf(&md5_str[i * 2], 3, "%02x", output[i]);
    }
    mbedtls_md5_free(&ctx);
    return std::string(md5_str);
}

std::string BLEManager::hashAuthorization(const std::string &url, const std::map<std::string, std::string> &params, const std::string &mac, const std::string &salt)
{
    std::string url_md5 = md5(url);
    std::string params_str = "";
    for (auto &param : params)
    {
        if (params_str.length() > 0)
        {
            params_str += "&";
        }
        params_str += param.first + "=" + param.second;
    }
    std::string params_md5 = md5(params_str);
    return md5(params_md5 + url_md5 + mac + salt);
}

std::string BLEManager::request(const std::string &method, const std::string &url, int16_t *status_code, const std::map<std::string, std::string> &headers, const std::map<std::string, std::string> &payload)
{
    auto &board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");
    for (auto &header : headers)
    {
        http->SetHeader(header.first, header.second);
    }
    if (payload.size() > 0)
    {
        std::string data = "{";
        for (auto &kv : payload)
        {
            if (data.length() > 1)
            {
                data += ",";
            }
            data += "\"" + kv.first + "\":\"" + kv.second + "\"";
        }
        data += "}";
        http->SetContent(std::move(data));
    }
    if (!http->Open(method, url))
    {
        *status_code = -1;
        return "";
    }
    *status_code = http->GetStatusCode();
    ;
    std::string res = "";
    if (*status_code == 200)
    {
        res = http->ReadAll();
    }
    http->Close();
    return res;
}

std::string generateRandomString(size_t length) {
    // 定义可用字符集（62个字符）
    const std::string chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    // 使用当前时间作为随机种子
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 generator(seed);  // 梅森旋转算法随机数生成器
    std::uniform_int_distribution<> distribution(0, chars.size() - 1);
    
    std::string result;
    result.reserve(length);  // 预分配内存
    
    for (size_t i = 0; i < length; ++i) {
        // 随机选择一个字符
        result += chars[distribution(generator)];
    }
    
    return result;
}

bool BLEManager::r_postDeviceName(const std::string &deviceId, const std::string &deviceName)
{
    Settings settings("ha_url");
    std::string haUrl = settings.GetString("url", "");
    std::string haApi = "/api/houzzkit-ai/update/speakname";

    std::map<std::string, std::string> params;
    params["speak_name"] = deviceName;
    params["speak_id"] = deviceId;
    std::string mac = SystemInfo::GetMacAddress();
    std::string salt = generateRandomString(16);
    std::string auth = hashAuthorization(haApi, params, mac, salt);
    std::map<std::string, std::string> headers;
    headers["Authorization"] = auth;
    headers["Salt"] = salt;

    int16_t status_code = 0;
    std::string res = this->request("POST", haUrl+haApi, &status_code, headers,params);
    if (status_code != 200)
    {
        return false;
    }
    return true;
}

void BLEManager::protoCheck()
{
    std::string url = "/api/houzzkit-assist/update/speakname";
    std::map<std::string, std::string> params;
    params["speak_name"] = "小瑞";
    params["speak_id"] = "49395dc1c03eae5deec8533f119f0662";
    // std::string mac = SystemInfo::getBleMacAddress();
    std::string mac = "10:b4:1d:e7:c3:aa";
    std::string salt = "asap1";
    std::string auth = hashAuthorization(url, params, mac, salt);

    // this->request();
}

void BLEManager::registerProto()
{
    // token 更新CMD
    _protoCallbackMap[CMD_UPDATE_TOKEN] = [this](const uint8_t *payload, uint16_t length)
    {
        std::string token_str = _protoParse.popString8();
        if (_protoParse.isConfiguringWifi())
        {
            if (_protoParse.updateClientToken(token_str))
            {
                _protoParse.protoBegin(CMD_UPDATE_TOKEN).pushUint8(0).protoSend();
            }
            else
            {
                _protoParse.protoBegin(CMD_UPDATE_TOKEN).pushUint8(2).protoSend();
            }
        }
        else
        {
            _protoParse.protoBegin(CMD_UPDATE_TOKEN).pushUint8(1).protoSend();
        }
        return true;
    };

    _protoCallbackMap[CMD_GET_DEVICE_INFO] = [this](const uint8_t *payload, uint16_t length)
    {
        _protoParse.protoBegin(CMD_GET_DEVICE_INFO)
            .pushUint8(0)
            .pushString8(SystemInfo::GetMacAddress())
            .pushString8(BOARD_NAME)
            .pushString8(esp_app_get_description()->version)
            .pushUint8(ESPHomeDevice::GetInstance().outputVolume())
            .pushUint8(ESPHomeDevice::GetInstance().micEnabled() ? 1 : 0)
            .pushUint8(ESPHomeDevice::GetInstance().continuousDialogue() ? 1 : 0)
            .pushUint8(ESPHomeDevice::GetInstance().voiceResponseSound() ? 1 : 0)
            .pushUint8(ESPHomeDevice::GetInstance().idleScreenOff() ? 1 : 0)
            .pushUint8(ESPHomeDevice::GetInstance().sleepMode() ? 1 : 0)
            .pushUint32(ESPHomeDevice::GetInstance().sleepModeTimeInterval())
            .protoSend();
        return true;
    };

    _protoCallbackMap[CMD_PUSH_ACCESS_POINTS] = [this](const uint8_t *payload, uint16_t length)
    {
        this->pushAccessPoints();
        this->_protoParse.protoBegin(CMD_PUSH_ACCESS_POINTS).pushUint8(0).protoSend();
        return true;
    };

    // CMD 3 ，push access points

    _protoCallbackMap[CMD_CONNECT_WIFI] = [this](const uint8_t *payload, uint16_t length)
    {
        this->stopPushAccessPoints();
        auto &wifi_station = WifiConfigurationAp::GetInstance();
        std::string ssid_str = _protoParse.popString8();
        std::string password_str = _protoParse.popString8();
        Application::GetInstance().Alert(Lang::Strings::WIFI_CONFIG_MODE, Lang::Strings::WIFI_CONFIG_MODE_CONNECTING, "gear", Lang::Sounds::OGG_POPUP);
        if (wifi_station.ConnectToWifi(ssid_str, password_str))
        {
            wifi_station.Save(ssid_str, password_str);
            auto app_desc = esp_app_get_description();
            _protoParse.protoBegin(CMD_CONNECT_WIFI)
                .pushUint8(0)
                .pushString8(SystemInfo::getBleMacAddress())
                .pushString8(SystemInfo::GetMacAddress())
                .pushString8(BOARD_NAME)
                .pushString8(app_desc->version)
                .protoSend();
        }
        else
        {
            std::string hint = Lang::Strings::BLE_NET_CONFIG;
            hint += "\n";
            hint += Board::GetInstance().getDeviceName();
            hint += "\n\n";
            Application::GetInstance().Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear", Lang::Sounds::OGG_EXCLAMATION);
            _protoParse.protoBegin(CMD_CONNECT_WIFI).pushUint8(1).protoSend();
        }
        return true;
    };

    _protoCallbackMap[CMD_CONFIG_WEBSOCKET] = [this](const uint8_t *payload, uint16_t length)
    {
        _protoParse.setConfiguredWifi();
        std::string wsUrl = _protoParse.popString16();
        std::string token = _protoParse.popString8();
        Settings settings("websocket", true);
        if (settings.GetString("url") != wsUrl)
        {
            settings.SetString("url", wsUrl);
        }
        if (settings.GetString("token") != token)
        {
            settings.SetString("token", token);
        }
        _protoParse.protoBegin(CMD_CONFIG_WEBSOCKET)
            .pushUint8(0)
            .protoSend();
        xTaskCreate([](void *ctx)
                    {vTaskDelay(pdMS_TO_TICKS(500));esp_restart(); }, "reboot_task", 4096, nullptr, 5, NULL);
        return true;
    };

    _protoCallbackMap[CMD_CONFIG_HOME_ASSISTANT] = [this](const uint8_t *payload, uint16_t length)
    {
        std::string encryptionKey = _protoParse.popString8();
        std::string noisePsk = _protoParse.popString8();
        std::string haUrl = _protoParse.popString8();
        std::string haApi = _protoParse.popString8();
        std::string token_str = _protoParse.popString8();
        std::string mcpEndpoint = _protoParse.popString8();
        std::string deviceId = _protoParse.popString8();

        ESPHomeDevice::GetInstance().setNoisePsk(noisePsk);
        std::map<std::string, std::string> params;
        params["host"] = WifiStation::GetInstance().GetIpAddress();
        params["port"] = "6053";
        params["noise_psk"] = encryptionKey;
        params["mcp_endpoint"] = mcpEndpoint + "?token=" + token_str;
        params["speak_id"] = deviceId;

        int16_t status_code = 0;
        std::string res = request("POST", haUrl + haApi, &status_code, {}, params);
        if (status_code != 200)
        {
            _protoParse.protoBegin(CMD_CONFIG_HOME_ASSISTANT)
                .pushUint8(1)
                .pushInt16(status_code)
                .protoSend();
            return true;
        }
        _protoParse.protoBegin(CMD_CONFIG_HOME_ASSISTANT)
            .pushUint8(0)
            .protoSend();
        Settings settings("ha_url", true);
        if (settings.GetString("url") != haUrl)
        {
            settings.SetString("url", haUrl);
        }
        return true;
    };

    _protoCallbackMap[CMD_OTA_UPDATE] = [this](const uint8_t *payload, uint16_t length)
    {
        std::string url = _protoParse.popString8();
        std::string version = _protoParse.popString8();
        _protoParse.protoBegin(CMD_OTA_UPDATE)
            .pushUint8(0)
            .protoSend();
        if (!otaStart(url, version))
        {
            return false;
        }
        return true;
    };

    _protoCallbackMap[CMD_DEVICE_SETTINGS] = [this](const uint8_t *payload, uint16_t length)
    {
        bool result = false;
        uint8_t settingType = _protoParse.popUint8();
        switch (settingType)
        {
        case PROPERTY_MIC_ENABLED:
        {
            uint8_t micEnable = _protoParse.popUint8();
            ESPHomeDevice::GetInstance().setMicEnable(micEnable == 1);
            result = true;
        }
        break;
        case PROPERTY_VOLUME:
        {
            uint8_t volume = _protoParse.popUint8();
            ESPHomeDevice::GetInstance().setOutputVolume(volume);
            result = true;
        }
        break;
        case PROPERTY_DEVICE_NAME:
        {
            std::string deviceId = _protoParse.popString8();
            std::string deviceName = _protoParse.popString8();
            result = r_postDeviceName(deviceId, deviceName);
        }
        break;
        case PROPERTY_CONTINUOUS_DIALOGUE:
        {
            uint8_t continuousDialogue = _protoParse.popUint8();
            ESPHomeDevice::GetInstance().setContinuousDialogue(continuousDialogue == 1);
            result = true;
        }
        break;
        case PROPERTY_VOICE_RESPONSE_SOUND:
        {
            uint8_t voiceResponseSound = _protoParse.popUint8();
            ESPHomeDevice::GetInstance().setVoiceResponseSound(voiceResponseSound == 1);
            result = true;
        }
        break;
        case PROPERTY_IDLE_SCREEN_OFF:
        {
            uint8_t idleScreenOff = _protoParse.popUint8();
            ESPHomeDevice::GetInstance().setIdleScreenOff(idleScreenOff == 1);
            result = true;
        }
        break;
        case PROPERTY_SLEEP_MODE:
        {
            uint8_t sleepMode = _protoParse.popUint8();
            ESPHomeDevice::GetInstance().setSleepMode(sleepMode == 1);
            result = true;
        }
        break;
        case PROPERTY_SLEEP_MODE_TIME_INTERVAL:
        {
            uint32_t sleepModeTimeInterval = _protoParse.popUint32();
            ESPHomeDevice::GetInstance().setSleepModeTimeInterval(sleepModeTimeInterval);
            result = true;
        }
        break;
        default:        
            break;
        }
        _protoParse.protoBegin(CMD_DEVICE_SETTINGS)
            .pushUint8(result ? 0 : 1)
            .protoSend();
        return true;
    };
}