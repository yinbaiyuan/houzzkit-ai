#include "mqtt_protocol.h"
#include "board.h"
#include "application.h"
#include "settings.h"
#include "audio_service.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
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
            if (app.GetDeviceState() != kDeviceStateRunning || !Board::GetInstance().GetVoiceController()->IsIdle()) {
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
    subscribe_topic_ = settings.GetString("subscribe_topic");
    if (subscribe_topic_.empty()) {
        subscribe_topic_ = client_id;
    }

    if (endpoint.empty()) {
        ESP_LOGW(TAG, "MQTT endpoint is not specified");
        if (report_error) {
            SetError(Lang::Strings::SERVICE_CONFIG_ERROR);
        }
        return false;
    }
    if (client_id.empty() || publish_topic_.empty() || subscribe_topic_.empty()) {
        ESP_LOGW(TAG, "MQTT config is incomplete: client_id=%s publish_topic=%s subscribe_topic=%s",
            client_id.empty() ? "empty" : "set",
            publish_topic_.empty() ? "empty" : "set",
            subscribe_topic_.empty() ? "empty" : "set");
        if (report_error) {
            SetError(Lang::Strings::SERVICE_CONFIG_ERROR);
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
        if (IsHandshakeInProgress()) {
            if (!Board::GetInstance().IsNetworkReady()) {
                SignalHandshakeFailure(MqttHandshakeFailure::NetworkDisconnected, "network disconnected during session setup");
            } else {
                SignalHandshakeFailure(MqttHandshakeFailure::HandshakeBreak, "MQTT disconnected during session setup");
            }
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
        SubscribeDownlinkTopic();
        SendTimeSyncRequest();
    });

    mqtt_->OnError([this](const std::string& error) {
        ESP_LOGE(TAG, "MQTT error: %s", error.c_str());
        if (IsHandshakeInProgress()) {
            SignalHandshakeFailure(MqttHandshakeFailure::ServiceConnectError, error);
        }
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

        if (strcmp(type->valuestring, "time_sync") == 0) {
            HandleTimeSyncMessage(root, payload);
        } else if (strcmp(type->valuestring, "hello") == 0) {
            if (!ParseServerHello(root)) {
                SignalHandshakeFailure(MqttHandshakeFailure::ServiceDataError, "invalid service session response");
            }
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
    int broker_port = 0;
    if (!ParseEndpoint(endpoint, broker_address, broker_port)) {
        ESP_LOGE(TAG, "Invalid MQTT endpoint: %s", endpoint.c_str());
        if (report_error) {
            SetError(Lang::Strings::SERVICE_CONFIG_ERROR);
        }
        return false;
    }
    if (!CheckHostReachable(broker_address, "broker")) {
        if (report_error) {
            SetError(Lang::Strings::DNS_RESOLVE_FAILED);
        }
        return false;
    }
    if (!mqtt_->Connect(broker_address, broker_port, client_id, username, password)) {
        ESP_LOGE(TAG, "Failed to connect to endpoint, reason=%d detail=%s",
            static_cast<int>(mqtt_->LastConnectError()), mqtt_->LastConnectErrorMessage().c_str());
        ScheduleReconnect();
        if (report_error) {
            SetError(MessageForConnectError(mqtt_->LastConnectError()));
        }
        return false;
    }

    ESP_LOGI(TAG, "Connected to endpoint");
    return true;
}

bool MqttProtocol::ParseEndpoint(const std::string& endpoint, std::string& broker_address, int& broker_port) {
    broker_port = 8883;
    auto pos = endpoint.find(':');
    if (pos == std::string::npos) {
        broker_address = endpoint;
        return !broker_address.empty();
    }

    broker_address = endpoint.substr(0, pos);
    auto port_text = endpoint.substr(pos + 1);
    if (broker_address.empty() || port_text.empty()) {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    long port = std::strtol(port_text.c_str(), &end, 10);
    if (errno != 0 || end == port_text.c_str() || *end != '\0' || port <= 0 || port > 65535) {
        return false;
    }
    broker_port = static_cast<int>(port);
    return true;
}

bool MqttProtocol::CheckHostReachable(const std::string& host, const char* context) {
    if (host.empty()) {
        ESP_LOGE(TAG, "Empty host for %s", context);
        return false;
    }

    in_addr addr4 = {};
    if (inet_pton(AF_INET, host.c_str(), &addr4) == 1) {
        return true;
    }

    // ML307/EC801E 等蜂窝模组在模组内部解析域名，主控侧 DNS 不一定可用。
    if (Board::GetInstance().GetBoardType() != "wifi") {
        return true;
    }

    addrinfo hints = {};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    int ret = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (result != nullptr) {
        freeaddrinfo(result);
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "%s host resolve failed: %s, ret=%d", context, host.c_str(), ret);
        return false;
    }
    return true;
}

const char* MqttProtocol::MessageForConnectError(MqttConnectError error) const {
    switch (error) {
        case MqttConnectError::Timeout:
            return Lang::Strings::SERVICE_CONNECT_TIMEOUT;
        case MqttConnectError::DnsFailed:
            return Lang::Strings::DNS_RESOLVE_FAILED;
        case MqttConnectError::AuthFailed:
            return Lang::Strings::AUTH_FAILED;
        case MqttConnectError::ProtocolRejected:
            return Lang::Strings::PROTOCOL_REJECTED;
        case MqttConnectError::ClientIdRejected:
            return Lang::Strings::CLIENT_ID_REJECTED;
        case MqttConnectError::Rejected:
            return Lang::Strings::SERVICE_REJECTED;
        case MqttConnectError::Failed:
        case MqttConnectError::None:
        default:
            return Lang::Strings::SERVICE_CONNECT_FAILED;
    }
}

const char* MqttProtocol::MessageForHandshakeFailure(MqttHandshakeFailure failure) const {
    switch (failure) {
        case MqttHandshakeFailure::NetworkDisconnected:
            return Lang::Strings::NETWORK_DISCONNECTED;
        case MqttHandshakeFailure::HandshakeBreak:
            return Lang::Strings::HANDSHAKE_BREAK;
        case MqttHandshakeFailure::ServiceConnectError:
            return Lang::Strings::SERVICE_CONNECT_ERROR;
        case MqttHandshakeFailure::ServiceDataError:
            return Lang::Strings::SERVICE_DATA_ERROR;
        case MqttHandshakeFailure::None:
        default:
            return Lang::Strings::SERVICE_REPLY_TIMEOUT;
    }
}

void MqttProtocol::BeginHandshake() {
    std::lock_guard<std::mutex> lock(handshake_mutex_);
    handshake_in_progress_ = true;
    handshake_failure_ = MqttHandshakeFailure::None;
    last_mqtt_error_.clear();
}

void MqttProtocol::EndHandshake() {
    std::lock_guard<std::mutex> lock(handshake_mutex_);
    handshake_in_progress_ = false;
}

bool MqttProtocol::IsHandshakeInProgress() {
    std::lock_guard<std::mutex> lock(handshake_mutex_);
    return handshake_in_progress_;
}

void MqttProtocol::SignalHandshakeFailure(MqttHandshakeFailure failure, const std::string& detail) {
    {
        std::lock_guard<std::mutex> lock(handshake_mutex_);
        if (!handshake_in_progress_) {
            return;
        }
        handshake_failure_ = failure;
        last_mqtt_error_ = detail;
    }
    ESP_LOGE(TAG, "Session setup failed: %s", detail.c_str());
    xEventGroupSetBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_FAILED_EVENT);
}

bool MqttProtocol::PublishBestEffort(const std::string& text, const char* context) {
    if (publish_topic_.empty()) {
        ESP_LOGW(TAG, "Skip %s publish: publish_topic is not specified", context);
        return false;
    }
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        ESP_LOGW(TAG, "Skip %s publish: MQTT is not connected", context);
        return false;
    }
    if (!mqtt_->Publish(publish_topic_, text)) {
        ESP_LOGW(TAG, "Failed to publish %s message: %s", context, text.c_str());
        return false;
    }
    return true;
}

void MqttProtocol::SendTimeSyncRequest() {
    if (PublishBestEffort("{\"type\":\"time_sync\"}", "time_sync")) {
        ESP_LOGI(TAG, "time_sync request sent");
    } else {
        ESP_LOGW(TAG, "time_sync request not sent");
    }
}

bool MqttProtocol::SubscribeDownlinkTopic() {
    if (subscribe_topic_.empty()) {
        ESP_LOGW(TAG, "Skip MQTT subscribe: subscribe_topic is not specified");
        return false;
    }
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        ESP_LOGW(TAG, "Skip MQTT subscribe: MQTT is not connected");
        return false;
    }
    if (!mqtt_->Subscribe(subscribe_topic_)) {
        ESP_LOGW(TAG, "Failed to subscribe MQTT topic: %s", subscribe_topic_.c_str());
        return false;
    }
    ESP_LOGI(TAG, "Subscribed MQTT topic: %s", subscribe_topic_.c_str());
    return true;
}

bool MqttProtocol::SendText(const std::string& text) {
    if (publish_topic_.empty()) {
        ESP_LOGE(TAG, "MQTT publish topic missing");
        SetError(Lang::Strings::SERVICE_CONFIG_ERROR);
        return false;
    }
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        ESP_LOGE(TAG, "MQTT is not connected when publishing message");
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    if (!mqtt_->Publish(publish_topic_, text)) {
        ESP_LOGE(TAG, "Failed to publish message: %s", text.c_str());
        SetError(Lang::Strings::SERVER_ERROR);
        return false;
    }
    return true;
}

bool MqttProtocol::SendHelloText(const std::string& text) {
    if (publish_topic_.empty()) {
        ESP_LOGE(TAG, "MQTT publish topic missing");
        SetError(Lang::Strings::SERVICE_CONFIG_ERROR);
        return false;
    }
    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        ESP_LOGE(TAG, "MQTT is not connected when sending session setup request");
        SetError(Lang::Strings::REQUEST_SEND_FAILED);
        return false;
    }
    if (!mqtt_->Publish(publish_topic_, text)) {
        ESP_LOGE(TAG, "MQTT hello publish failed: %s", text.c_str());
        SetError(Lang::Strings::REQUEST_SEND_FAILED);
        return false;
    }
    return true;
}

bool MqttProtocol::HandleTimeSyncMessage(const cJSON* root, const std::string& payload) {
    ESP_LOGI(TAG, "Received time_sync response");

    auto server_time = cJSON_GetObjectItem(root, "server_time");
    if (!cJSON_IsObject(server_time)) {
        ESP_LOGW(TAG, "Invalid time_sync message: missing server_time");
        return true;
    }

    auto timestamp = cJSON_GetObjectItem(server_time, "timestamp");
    if (!cJSON_IsNumber(timestamp)) {
        ESP_LOGW(TAG, "Invalid time_sync message: missing timestamp");
        return true;
    }

    int64_t timestamp_ms = static_cast<int64_t>(timestamp->valuedouble);
    if (timestamp_ms <= 0) {
        ESP_LOGW(TAG, "Invalid time_sync timestamp: sec=%ld ms_part=%03ld",
            static_cast<long>(timestamp_ms / 1000),
            static_cast<long>(timestamp_ms % 1000));
        return true;
    }

    struct timeval tv = {};
    tv.tv_sec = timestamp_ms / 1000;
    tv.tv_usec = (timestamp_ms % 1000) * 1000;
    if (settimeofday(&tv, nullptr) != 0) {
        ESP_LOGW(TAG, "Failed to set system time from time_sync");
        return true;
    }

    ESP_LOGI(TAG, "System time synced: %s", payload.c_str());
    Application::GetInstance().SetServerTimeSynced(true);
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

    if (udp_->Send(encrypted) <= 0) {
        return false;
    }
    return true;
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
    if (!Board::GetInstance().IsNetworkReady()) {
        ESP_LOGE(TAG, "Network is not ready before MQTT request");
        SetError(Lang::Strings::NETWORK_DISCONNECTED);
        return false;
    }

    if (mqtt_ == nullptr || !mqtt_->IsConnected()) {
        ESP_LOGI(TAG, "MQTT is not connected, try to connect now");
        if (!StartMqttClient(true)) {
            return false;
        }
    }
    if (!SubscribeDownlinkTopic()) {
        ESP_LOGE(TAG, "Failed to subscribe service reply topic before session setup");
        SetError(Lang::Strings::REPLY_CHANNEL_FAILED);
        return false;
    }

    error_occurred_ = false;
    session_id_ = "";
    xEventGroupClearBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT | MQTT_PROTOCOL_SERVER_HELLO_FAILED_EVENT);
    BeginHandshake();

    auto message = GetHelloMessage();
    if (!SendHelloText(message)) {
        EndHandshake();
        return false;
    }

    // 绛夊緟鏈嶅姟鍣ㄥ搷搴?
    EventBits_t bits = xEventGroupWaitBits(event_group_handle_,
        MQTT_PROTOCOL_SERVER_HELLO_EVENT | MQTT_PROTOCOL_SERVER_HELLO_FAILED_EVENT,
        pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (bits & MQTT_PROTOCOL_SERVER_HELLO_FAILED_EVENT) {
        MqttHandshakeFailure failure = MqttHandshakeFailure::None;
        std::string detail;
        {
            std::lock_guard<std::mutex> lock(handshake_mutex_);
            failure = handshake_failure_;
            detail = last_mqtt_error_;
        }
        EndHandshake();
        ESP_LOGE(TAG, "Failed to setup session: %s", detail.c_str());
        SetError(MessageForHandshakeFailure(failure));
        return false;
    }
    if (!(bits & MQTT_PROTOCOL_SERVER_HELLO_EVENT)) {
        EndHandshake();
        ESP_LOGE(TAG, "MQTT hello no response within 10000 ms");
        SetError(Lang::Strings::SERVICE_REPLY_TIMEOUT);
        return false;
    }
    EndHandshake();

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

    if (!CheckHostReachable(udp_server_, "audio channel")) {
        udp_.reset();
        SetError(Lang::Strings::DNS_RESOLVE_FAILED);
        return false;
    }

    if (!udp_->Connect(udp_server_, udp_port_)) {
        ESP_LOGE(TAG, "Failed to open audio channel: %s:%d", udp_server_.c_str(), udp_port_);
        udp_.reset();
        SetError(Lang::Strings::AUDIO_CHANNEL_OPEN_FAILED);
        return false;
    }
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
#if CONFIG_USE_VOICE_DIALOGUE
    cJSON_AddStringToObject(root, "transport", "udp");
#else
    cJSON_AddStringToObject(root, "transport", "mqtt");
#endif
    cJSON* features = cJSON_CreateObject();
    bool feature_aec = false;
    bool feature_daec = false;
    bool supports_device_aec = false;
#if CONFIG_USE_VOICE_DIALOGUE
    supports_device_aec = Board::GetInstance().GetVoiceController()->GetAudioService().SupportsDeviceAec();
#endif
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
#if CONFIG_USE_VOICE_DIALOGUE
    cJSON* audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", "opus");
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    cJSON_AddNumberToObject(audio_params, "frame_duration", kUdpDownlinkFrameDurationMs);
    cJSON_AddBoolToObject(audio_params, "fec", kUdpDownlinkOpusFecEnabled);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
#endif
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

bool MqttProtocol::ParseServerHello(const cJSON* root) {
    auto transport = cJSON_GetObjectItem(root, "transport");
    if (!cJSON_IsString(transport) || strcmp(transport->valuestring, "udp") != 0) {
        ESP_LOGE(TAG, "Invalid hello transport");
        return false;
    }

    std::string next_session_id;
    int next_sample_rate = server_sample_rate_;
    int next_frame_duration = server_frame_duration_;

    auto session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        next_session_id = session_id->valuestring;
    }

    // Get sample rate from hello message
    auto audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        auto sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            next_sample_rate = sample_rate->valueint;
        }
        auto frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            next_frame_duration = frame_duration->valueint;
        }
        auto fec = cJSON_GetObjectItem(audio_params, "fec");
        server_downlink_fec_ = cJSON_IsBool(fec) ? cJSON_IsTrue(fec) : false;
    }

    auto udp = cJSON_GetObjectItem(root, "udp");
    if (!cJSON_IsObject(udp)) {
        ESP_LOGE(TAG, "Missing audio channel params");
        return false;
    }
    auto server = cJSON_GetObjectItem(udp, "server");
    auto port = cJSON_GetObjectItem(udp, "port");
    auto key_item = cJSON_GetObjectItem(udp, "key");
    auto nonce_item = cJSON_GetObjectItem(udp, "nonce");
    if (!cJSON_IsString(server) || !cJSON_IsNumber(port) ||
        !cJSON_IsString(key_item) || !cJSON_IsString(nonce_item) ||
        port->valueint <= 0 || port->valueint > 65535) {
        ESP_LOGE(TAG, "Missing or invalid audio channel params");
        return false;
    }

    std::string key = key_item->valuestring;
    std::string nonce = nonce_item->valuestring;
    if (!IsValidHexString(key, 16) || !IsValidHexString(nonce)) {
        ESP_LOGE(TAG, "Invalid audio channel encryption params");
        return false;
    }
    std::string next_udp_server = server->valuestring;
    int next_udp_port = port->valueint;

    // auto encryption = cJSON_GetObjectItem(udp, "encryption")->valuestring;
    // ESP_LOGI(TAG, "UDP server: %s, port: %d, encryption: %s", udp_server_.c_str(), udp_port_, encryption);
    std::string next_aes_nonce = DecodeHexString(nonce);
    auto decoded_key = DecodeHexString(key);

    session_id_ = next_session_id;
    server_sample_rate_ = next_sample_rate;
    server_frame_duration_ = next_frame_duration;
    udp_server_ = next_udp_server;
    udp_port_ = next_udp_port;
    aes_nonce_ = next_aes_nonce;
    mbedtls_aes_init(&aes_ctx_);
    mbedtls_aes_setkey_enc(&aes_ctx_, (const unsigned char*)decoded_key.data(), 128);
    LogDownlinkSequenceStats("reset");
    local_sequence_ = 0;
    remote_sequence_ = 0;
    ResetDownlinkSequenceStats();
    ESP_LOGI(TAG, "Session ID: %s", session_id_.c_str());
    xEventGroupSetBits(event_group_handle_, MQTT_PROTOCOL_SERVER_HELLO_EVENT);
    return true;
}

static const char hex_chars[] = "0123456789ABCDEF";
// 辅助函数，将单个十六进制字符转换为对应的数值
static inline uint8_t CharToHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;  // 对于无效输入，返回
}

bool MqttProtocol::IsValidHexString(const std::string& text, size_t expected_decoded_size) const {
    if (text.empty() || (text.size() % 2) != 0) {
        return false;
    }
    if (expected_decoded_size != 0 && text.size() != expected_decoded_size * 2) {
        return false;
    }
    for (char c : text) {
        bool valid = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
        if (!valid) {
            return false;
        }
    }
    return true;
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
