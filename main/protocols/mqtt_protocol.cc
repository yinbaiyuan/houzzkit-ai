#include "mqtt_protocol.h"
#include "board.h"
#include "application.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <arpa/inet.h>
#include "assets/lang_config.h"

#define TAG "MQTT"

namespace {
void CheckHeapIntegrity(const char* stage) {
#if CONFIG_IDF_TARGET_ESP32P4
    if (!heap_caps_check_integrity_all(false)) {
        ESP_LOGE(TAG, "Heap corruption detected at %s", stage);
        heap_caps_check_integrity_all(true);
    }
#else
    (void)stage;
#endif
}

constexpr int kUdpDownlinkFrameDurationMs = 20;
constexpr bool kUdpDownlinkOpusFecEnabled = true;
constexpr uint32_t kDownlinkSequenceDebugLogLimit = 4;

}  // namespace

MqttProtocol::MqttProtocol() {
    local_sequence_ = 0;
    remote_sequence_ = 0;
    ResetDownlinkSequenceStats();
    event_group_handle_ = xEventGroupCreate();
    // Initialize reconnect timer
    esp_timer_create_args_t reconnect_timer_args = {
        .callback = [](void* arg) {
            MqttProtocol* protocol = (MqttProtocol*)arg;
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() != kDeviceStateIdle) {
                ESP_LOGI(TAG, "Skip MQTT reconnect because device is busy, retry later");
                protocol->ScheduleReconnect();
                return;
            }

            ESP_LOGI(TAG, "Reconnecting to MQTT server");
            app.Schedule([protocol]() {
                if (!protocol->StartMqttClient(false)) {
                    protocol->ScheduleReconnect();
                }
            });
        },
        .arg = this,
    };
    esp_timer_create(&reconnect_timer_args, &reconnect_timer_);


}

MqttProtocol::~MqttProtocol() {
    ESP_LOGI(TAG, "MqttProtocol deinit");
    if (reconnect_timer_ != nullptr) {
        esp_timer_stop(reconnect_timer_);
        esp_timer_delete(reconnect_timer_);
    }

    udp_.reset();
    ResetMqttClient("destructor");

    if (event_group_handle_ != nullptr) {
        vEventGroupDelete(event_group_handle_);
    }
}

bool MqttProtocol::Start() {
    return StartMqttClient(false);
}

void MqttProtocol::ScheduleReconnect() {
    if (reconnect_timer_ == nullptr) {
        return;
    }
    if (esp_timer_is_active(reconnect_timer_)) {
        esp_timer_stop(reconnect_timer_);
    }
    ESP_LOGI(TAG, "Scheduling MQTT reconnect in %d seconds", MQTT_RECONNECT_INTERVAL_MS / 1000);
    esp_timer_start_once(reconnect_timer_, MQTT_RECONNECT_INTERVAL_MS * 1000);
}

void MqttProtocol::ResetMqttClient(const char* reason) {
    if (mqtt_ == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Resetting MQTT client, reason=%s", reason);
    CheckHeapIntegrity("mqtt_reset_before_disconnect");
    ignore_mqtt_disconnect_.store(true);
    auto mqtt = std::move(mqtt_);
    mqtt->Disconnect();
    mqtt->OnConnected({});
    mqtt->OnDisconnected({});
    mqtt->OnMessage({});
    mqtt->OnError({});
    mqtt.reset();
    ignore_mqtt_disconnect_.store(false);
    CheckHeapIntegrity("mqtt_reset_after_disconnect");
}

bool MqttProtocol::StartMqttClient(bool report_error) {
    if (mqtt_ != nullptr) {
        ESP_LOGW(TAG, "MQTT client already exists, recreate it after disconnect");
        CloseAudioChannelInternal("mqtt_client_restart", false);
        ResetMqttClient("restart");
    }

    Settings settings("mqtt", false);
    auto endpoint = settings.GetString("endpoint");
    auto client_id = settings.GetString("client_id");
    auto username = settings.GetString("username");
    auto password = settings.GetString("password");
    int keepalive_interval = settings.GetInt("keepalive", 240);
    publish_topic_ = settings.GetString("publish_topic");

    if (endpoint.empty()) {
        ESP_LOGW(TAG, "MQTT endpoint is not specified");
        if (report_error) {
            SetError(Lang::Strings::SERVER_NOT_FOUND);
        }
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    mqtt_ = network->CreateMqtt(0);
    mqtt_->SetKeepAlive(keepalive_interval);

    mqtt_->OnDisconnected([this]() {
        if (ignore_mqtt_disconnect_.load()) {
            ESP_LOGI(TAG, "Ignore MQTT disconnected callback during client reset");
            return;
        }
        if (on_disconnected_ != nullptr) {
            on_disconnected_();
        }
        bool has_active_audio_channel = false;
        std::string current_session_id;
        {
            std::lock_guard<std::mutex> lock(channel_mutex_);
            has_active_audio_channel = udp_ != nullptr || !session_id_.empty();
            current_session_id = session_id_;
        }
        ESP_LOGI(TAG, "MQTT disconnected, active_audio_channel=%d session_id=%s",
            has_active_audio_channel ? 1 : 0,
            current_session_id.empty() ? "<empty>" : current_session_id.c_str());
        ScheduleReconnect();
    });

    mqtt_->OnConnected([this]() {
        if (on_connected_ != nullptr) {
            on_connected_();
        }
        ESP_LOGI(TAG, "MQTT connected, stop reconnect timer");
        esp_timer_stop(reconnect_timer_);
    });

    mqtt_->OnMessage([this](const std::string& topic, const std::string& payload) {
        cJSON* root = cJSON_Parse(payload.c_str());
        if (root == nullptr) {
            ESP_LOGE(TAG, "Failed to parse json message %s", payload.c_str());
            return;
        }
        cJSON* type = cJSON_GetObjectItem(root, "type");
        if (!cJSON_IsString(type)) {
            ESP_LOGE(TAG, "Message type is invalid");
            cJSON_Delete(root);
            return;
        }

        if (strcmp(type->valuestring, "hello") == 0) {
            ParseServerHello(root);
        } else if (strcmp(type->valuestring, "goodbye") == 0) {
            auto session_id = cJSON_GetObjectItem(root, "session_id");
            ESP_LOGI(TAG, "Received goodbye message, session_id: %s", session_id ? session_id->valuestring : "null");
            if (session_id == nullptr || session_id_ == session_id->valuestring) {
                Application::GetInstance().Schedule([this]() {
                    CloseAudioChannelInternal("server_goodbye", true);
                });
            }
        } else if (on_incoming_json_ != nullptr) {
            on_incoming_json_(root);
        }
        cJSON_Delete(root);
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    ESP_LOGI(TAG, "Connecting to endpoint %s", endpoint.c_str());
    std::string broker_address;
    int broker_port = 8883;
    size_t pos = endpoint.find(':');
    if (pos != std::string::npos) {
        broker_address = endpoint.substr(0, pos);
        broker_port = std::stoi(endpoint.substr(pos + 1));
    } else {
        broker_address = endpoint;
    }
    if (!mqtt_->Connect(broker_address, broker_port, client_id, username, password)) {
        ESP_LOGE(TAG, "Failed to connect to endpoint");
        ScheduleReconnect();
        SetError(Lang::Strings::SERVER_NOT_CONNECTED);
        return false;
    }

    ESP_LOGI(TAG, "Connected to endpoint");
    return true;
}

bool MqttProtocol::SendText(const std::string& text) {
    if (publish_topic_.empty()) {
        return false;
    }
    if (!mqtt_->Publish(publish_topic_, text)) {
        ESP_LOGE(TAG, "Failed to publish message: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return true;
}

bool MqttProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    std::lock_guard<std::mutex> lock(channel_mutex_);
    if (udp_ == nullptr) {
        return false;
    }

    std::string nonce(aes_nonce_);
    *(uint16_t*)&nonce[2] = htons(packet->payload.size());
    *(uint32_t*)&nonce[8] = htonl(packet->timestamp);
    *(uint32_t*)&nonce[12] = htonl(++local_sequence_);

    std::string encrypted;
    encrypted.resize(aes_nonce_.size() + packet->payload.size());
    memcpy(encrypted.data(), nonce.data(), nonce.size());

    size_t nc_off = 0;
    uint8_t stream_block[16] = {0};
    if (mbedtls_aes_crypt_ctr(&aes_ctx_, packet->payload.size(), &nc_off, (uint8_t*)nonce.c_str(), stream_block,
        (uint8_t*)packet->payload.data(), (uint8_t*)&encrypted[nonce.size()]) != 0) {
        ESP_LOGE(TAG, "Failed to encrypt audio data");
        return false;
    }

    return udp_->Send(encrypted) > 0;
}

void MqttProtocol::CloseAudioChannel() {
    CloseAudioChannelInternal("request", true);
}

bool MqttProtocol::CloseAudioChannelInternal(const char* reason, bool notify_application) {
    std::string closing_session_id;
    bool mqtt_connected = false;
    bool had_active_audio_channel = false;
    std::unique_ptr<Udp> udp_to_close;
    uint32_t next_udp_channel_generation = 0;

    {
        std::lock_guard<std::mutex> channel_lock(channel_mutex_);
        if (udp_ == nullptr && session_id_.empty()) {
            ESP_LOGI(TAG, "Skip closing audio channel, reason=%s active=0", reason);
            return false;
        }

        LogDownlinkSequenceStats(reason);
        ResetDownlinkSequenceStats();
        had_active_audio_channel = true;
        closing_session_id = session_id_;
        mqtt_connected = mqtt_ != nullptr && mqtt_->IsConnected();
        next_udp_channel_generation = udp_channel_generation_.fetch_add(1) + 1;
        udp_to_close = std::move(udp_);
        session_id_.clear();
        udp_server_.clear();
        udp_port_ = 0;
        aes_nonce_.clear();
        local_sequence_ = 0;
        error_occurred_ = false;
    }

    CheckHeapIntegrity("audio_channel_close_before_udp_disconnect");
    if (udp_to_close != nullptr) {
        udp_to_close->Disconnect();
    }
    CheckHeapIntegrity("audio_channel_close_after_udp_disconnect");

    bool sent_goodbye = false;
    if (mqtt_connected && !closing_session_id.empty()) {
        std::string message = "{";
        message += "\"session_id\":\"" + closing_session_id + "\",";
        message += "\"type\":\"goodbye\"";
        message += "}";
        sent_goodbye = SendText(message);
    }

    ESP_LOGI(TAG, "Audio channel closed, reason=%s active=%d notify=%d sent_goodbye=%d session_id=%s",
        reason,
        had_active_audio_channel ? 1 : 0,
        notify_application ? 1 : 0,
        sent_goodbye ? 1 : 0,
        closing_session_id.empty() ? "<empty>" : closing_session_id.c_str());
    ESP_LOGI(TAG, "Audio channel generation advanced to %lu", static_cast<unsigned long>(next_udp_channel_generation));

    if (notify_application && on_audio_channel_closed_ != nullptr) {
        on_audio_channel_closed_();
    }
    return true;
}

bool MqttProtocol::OpenAudioChannel() {
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        ESP_LOGI(TAG, "MQTT is not connected, try to connect now");
        if (!StartMqttClient(true)) {
            return false;
        }
    }

    error_occurred_ = false;
    session_id_ = "";
    xEventGroupClearBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT);

    auto message = GetHelloMessage();
    if (!SendText(message)) {
        return false;
    }

    // 等待服务器响应
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & MQTT_PROTOCOL_SERVER_HELLO_EVENT)) {
        ESP_LOGE(TAG, "Failed to receive server hello");
        SetError(Lang::Strings::SERVER_TIMEOUT);
        return false;
    }

    std::lock_guard<std::mutex> lock(channel_mutex_);
    auto network = Board::GetInstance().GetNetwork();
    uint32_t channel_generation = udp_channel_generation_.fetch_add(1) + 1;
    udp_ = network->CreateUdp(2);
    udp_->OnMessage([this, channel_generation](const std::string& data) {
        /*
         * UDP Encrypted OPUS Packet Format:
         * |type 1u|flags 1u|payload_len 2u|ssrc 4u|timestamp 4u|sequence 4u|
         * |payload payload_len|
         */
        std::lock_guard<std::mutex> channel_lock(channel_mutex_);
        if (channel_generation != udp_channel_generation_.load() || udp_ == nullptr) {
            ESP_LOGW(TAG, "Drop stale UDP packet for generation %lu, current=%lu",
                static_cast<unsigned long>(channel_generation),
                static_cast<unsigned long>(udp_channel_generation_.load()));
            return;
        }
        if (data.size() < sizeof(aes_nonce_)) {
            ESP_LOGE(TAG, "Invalid audio packet size: %u", data.size());
            return;
        }
        if (data[0] != 0x01) {
            ESP_LOGE(TAG, "Invalid audio packet type: %x", data[0]);
            return;
        }
        uint32_t timestamp = ntohl(*(uint32_t*)&data[8]);
        uint32_t sequence = ntohl(*(uint32_t*)&data[12]);

        size_t decrypted_size = data.size() - aes_nonce_.size();
        size_t nc_off = 0;
        uint8_t stream_block[16] = {0};
        auto nonce = (uint8_t*)data.data();
        auto encrypted = (uint8_t*)data.data() + aes_nonce_.size();
        auto packet = std::make_unique<AudioStreamPacket>();
        packet->sample_rate = server_sample_rate_;
        packet->frame_duration = server_frame_duration_;
        packet->timestamp = timestamp;
        packet->sequence = sequence;
        packet->payload.resize(decrypted_size);
        int ret = mbedtls_aes_crypt_ctr(&aes_ctx_, decrypted_size, &nc_off, nonce, stream_block, encrypted, (uint8_t*)packet->payload.data());
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to decrypt audio data, ret: %d", ret);
            return;
        }

        ObserveDownlinkSequence(sequence);

        if (on_incoming_audio_ != nullptr) {
            on_incoming_audio_(std::move(packet));
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    udp_->Connect(udp_server_, udp_port_);
    ESP_LOGI(TAG, "Opened UDP audio channel, generation=%lu", static_cast<unsigned long>(channel_generation));
    CheckHeapIntegrity("audio_channel_open_after_udp_connect");

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    return true;
}

void MqttProtocol::ObserveDownlinkSequence(uint32_t sequence) {
    if (sequence == 0) {
        return;
    }

    if (remote_sequence_ == 0) {
        remote_sequence_ = sequence;
        return;
    }

    if (sequence > remote_sequence_) {
        if (sequence > remote_sequence_ + 1) {
            const uint32_t gap = sequence - remote_sequence_ - 1;
            downlink_arrival_gap_events_++;
            if (gap > downlink_max_arrival_gap_) {
                downlink_max_arrival_gap_ = gap;
            }
            if (downlink_sequence_debug_logs_ < kDownlinkSequenceDebugLogLimit) {
                ESP_LOGD(TAG,
                    "Downlink arrival sequence gap: previous_high=%lu current=%lu gap=%lu gaps=%lu",
                    static_cast<unsigned long>(remote_sequence_),
                    static_cast<unsigned long>(sequence),
                    static_cast<unsigned long>(gap),
                    static_cast<unsigned long>(downlink_arrival_gap_events_));
                downlink_sequence_debug_logs_++;
            }
        }
        remote_sequence_ = sequence;
        return;
    }

    downlink_out_of_order_packets_++;
    if (downlink_sequence_debug_logs_ < kDownlinkSequenceDebugLogLimit) {
        ESP_LOGD(TAG,
            "Downlink out-of-order packet: seq=%lu high=%lu out_of_order=%lu",
            static_cast<unsigned long>(sequence),
            static_cast<unsigned long>(remote_sequence_),
            static_cast<unsigned long>(downlink_out_of_order_packets_));
        downlink_sequence_debug_logs_++;
    }
}

void MqttProtocol::ResetDownlinkSequenceStats() {
    remote_sequence_ = 0;
    downlink_arrival_gap_events_ = 0;
    downlink_max_arrival_gap_ = 0;
    downlink_out_of_order_packets_ = 0;
    downlink_sequence_debug_logs_ = 0;
}

void MqttProtocol::LogDownlinkSequenceStats(const char* reason) {
    if (downlink_arrival_gap_events_ == 0 && downlink_out_of_order_packets_ == 0) {
        return;
    }

    ESP_LOGI(TAG,
        "Downlink arrival summary: reason=%s high=%lu arrival_gaps=%lu max_gap=%lu out_of_order=%lu",
        reason,
        static_cast<unsigned long>(remote_sequence_),
        static_cast<unsigned long>(downlink_arrival_gap_events_),
        static_cast<unsigned long>(downlink_max_arrival_gap_),
        static_cast<unsigned long>(downlink_out_of_order_packets_));
}

std::string MqttProtocol::GetHelloMessage() {
    // 发送 hello 消息申请 UDP 通道
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", 3);
    cJSON_AddStringToObject(root, "transport", "udp");
    cJSON* features = cJSON_CreateObject();
    bool feature_aec = false;
    bool feature_daec = false;
    bool supports_device_aec = Application::GetInstance().GetAudioService().SupportsDeviceAec();
#if CONFIG_USE_SERVER_AEC
    feature_aec = true;
    cJSON_AddBoolToObject(features, "aec", true);
#elif CONFIG_USE_DEVICE_AEC
    feature_daec = supports_device_aec;
    if (feature_daec) {
        cJSON_AddBoolToObject(features, "daec", true);
    }
#endif
    cJSON_AddBoolToObject(features, "mcp", true);
    cJSON_AddItemToObject(root, "features", features);
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", kUdpDownlinkFrameDurationMs);
    cJSON_AddBoolToObject(audio_params, "fec", kUdpDownlinkOpusFecEnabled);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    auto json_str = cJSON_PrintUnformatted(root);
    std::string message(json_str);
    cJSON_free(json_str);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Hello features: aec=%d daec=%d mcp=1 supports_device_aec=%d",
        feature_aec ? 1 : 0,
        feature_daec ? 1 : 0,
        supports_device_aec ? 1 : 0);
    return message;
}

void MqttProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (transport == nullptr || strcmp(transport->valuestring, "udp") != 0) {
        ESP_LOGE(TAG, "Unsupported transport: %s", transport->valuestring);
        return;
    }

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        session_id_ = session_id->valuestring;
        ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    }

    // Get sample rate from hello message
    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            server_sample_rate_ = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            server_frame_duration_ = frame_duration->valueint;
        }
        auto fec = cJSON_GetObjectItem(audio_params, "fec");
        server_downlink_fec_ = cJSON_IsBool(fec) ? cJSON_IsTrue(fec) : false;
    }

    auto udp = cJSON_GetObjectItem(root, "udp");
    if (!cJSON_IsObject(udp)) {
        ESP_LOGE(TAG, "UDP is not specified");
        return;
    }
    udp_server_ = cJSON_GetObjectItem(udp, "server")->valuestring;
    udp_port_ = cJSON_GetObjectItem(udp, "port")->valueint;
    auto key = cJSON_GetObjectItem(udp, "key")->valuestring;
    auto nonce = cJSON_GetObjectItem(udp, "nonce")->valuestring;

    // auto encryption = cJSON_GetObjectItem(udp, "encryption")->valuestring;
    // ESP_LOGI(TAG, "UDP server: %s, port: %d, encryption: %s", udp_server_.c_str(), udp_port_, encryption);
    aes_nonce_ = DecodeHexString(nonce);
    mbedtls_aes_init(&aes_ctx_);
    mbedtls_aes_setkey_enc(&aes_ctx_, (const unsigned char*)DecodeHexString(key).c_str(), 128);
    LogDownlinkSequenceStats("reset");
    local_sequence_ = 0;
    ResetDownlinkSequenceStats();
    xEventGroupSetBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT);
}

static const char hex_chars[] = "0123456789ABCDEF";
// 辅助函数，将单个十六进制字符转换为对应的数值
static inline uint8_t CharToHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;  // 对于无效输入，返回0
}

std::string MqttProtocol::DecodeHexString(const std::string& hex_string) {
    std::string decoded;
    decoded.reserve(hex_string.size() / 2);
    for (size_t i = 0; i < hex_string.size(); i += 2) {
        char byte = (CharToHex(hex_string[i]) << 4) | CharToHex(hex_string[i + 1]);
        decoded.push_back(byte);
    }
    return decoded;
}

bool MqttProtocol::IsAudioChannelOpened() const {
    return udp_ != nullptr && !error_occurred_ && !IsTimeout();
}


bool MqttProtocol::SendEmptyAudioPacket() {
    if (!IsAudioChannelOpened()) {
        return false;
    }    

    auto packet = std::make_unique<AudioStreamPacket>();
    packet->frame_duration = OPUS_FRAME_DURATION_MS;
    packet->sample_rate = 16000;
    packet->timestamp = 1;
    packet->payload.resize(10, 0x01);
    SendAudio(std::move(packet));

    return true;   
}

void MqttProtocol::sendPlayVoiceText(const std::string& text)
{
    SendEmptyAudioPacket();
    Protocol::sendPlayVoiceText(text);
}
void MqttProtocol::sendExecuteCommandText(const std::string& command)
{
    SendEmptyAudioPacket();
    Protocol::sendExecuteCommandText(command);
}
void MqttProtocol::sendAskAndExecuteCommandText(const std::string& command)
{
    SendEmptyAudioPacket();
    Protocol::sendAskAndExecuteCommandText(command);
}
