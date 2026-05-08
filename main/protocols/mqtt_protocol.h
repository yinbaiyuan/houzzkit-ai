#ifndef MQTT_PROTOCOL_H
#define MQTT_PROTOCOL_H


#include "protocol.h"
#include <mqtt.h>
#include <udp.h>
#include <cJSON.h>
#include <mbedtls/aes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>

#include <functional>
#include <string>
#include <map>
#include <mutex>

#define MQTT_PING_INTERVAL_SECONDS 90
#define MQTT_RECONNECT_INTERVAL_MS 60000

#define MQTT_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)
#define MQTT_PROTOCOL_SERVER_HELLO_FAILED_EVENT (1 << 1)

enum class MqttHandshakeFailure {
    None,
    NetworkDisconnected,
    HandshakeBreak,
    ServiceConnectError,
    ServiceDataError,
};

class MqttProtocol : public Protocol {
public:
    MqttProtocol();
    ~MqttProtocol();

    bool Start() override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel() override;
    bool IsAudioChannelOpened() const override;

    virtual void sendPlayVoiceText(const std::string& text) override;
    virtual void sendExecuteCommandText(const std::string& command) override;
    virtual void sendAskAndExecuteCommandText(const std::string& command) override;

private:
    EventGroupHandle_t event_group_handle_;

    std::string publish_topic_;
    std::string subscribe_topic_;

    std::mutex channel_mutex_;
    std::unique_ptr<Mqtt> mqtt_;
    std::unique_ptr<Udp> udp_;
    mbedtls_aes_context aes_ctx_;
    std::string aes_nonce_;
    std::string udp_server_;
    int udp_port_;
    uint32_t local_sequence_;
    uint32_t remote_sequence_;
    uint32_t downlink_arrival_gap_events_;
    uint32_t downlink_max_arrival_gap_;
    uint32_t downlink_out_of_order_packets_;
    uint32_t downlink_sequence_debug_logs_;
    esp_timer_handle_t reconnect_timer_;    
    std::unique_ptr<AudioStreamPacket> last_valid_packet_;
    std::mutex last_packet_mutex_;
    std::mutex handshake_mutex_;
    bool handshake_in_progress_ = false;
    MqttHandshakeFailure handshake_failure_ = MqttHandshakeFailure::None;
    std::string last_mqtt_error_;

    bool StartMqttClient(bool report_error=false);
    bool ParseServerHello(const cJSON* root);
    bool HandleTimeSyncMessage(const cJSON* root, const std::string& payload);
    std::string DecodeHexString(const std::string& hex_string);

    bool SendText(const std::string& text) override;
    bool SendHelloText(const std::string& text);
    bool PublishBestEffort(const std::string& text, const char* context);
    bool SubscribeDownlinkTopic();
    bool CheckHostReachable(const std::string& host, const char* context);
    bool ParseEndpoint(const std::string& endpoint, std::string& broker_address, int& broker_port);
    const char* MessageForConnectError(MqttConnectError error) const;
    const char* MessageForHandshakeFailure(MqttHandshakeFailure failure) const;
    void BeginHandshake();
    void EndHandshake();
    bool IsHandshakeInProgress();
    void SignalHandshakeFailure(MqttHandshakeFailure failure, const std::string& detail);
    bool IsValidHexString(const std::string& text, size_t expected_decoded_size = 0) const;
    std::string GetHelloMessage();
    void SendTimeSyncRequest();

    bool SendEmptyAudioPacket();

    void ObserveDownlinkSequence(uint32_t sequence);
    void ResetDownlinkSequenceStats();
    void LogDownlinkSequenceStats(const char* reason);

};


#endif // MQTT_PROTOCOL_H
