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
#include <atomic>

#define MQTT_PING_INTERVAL_SECONDS 90
#define MQTT_RECONNECT_INTERVAL_MS 60000

#define MQTT_PROTOCOL_SERVER_HELLO_EVENT (1 << 0)

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

    std::mutex channel_mutex_;
    std::unique_ptr<Mqtt> mqtt_;
    std::unique_ptr<Udp> udp_;
    mbedtls_aes_context aes_ctx_;
    std::string aes_nonce_;
    std::string udp_server_;
    int udp_port_;
    uint32_t local_sequence_;
    uint32_t remote_sequence_;
    esp_timer_handle_t reconnect_timer_;    
    std::atomic<bool> ignore_mqtt_disconnect_{false};
    std::atomic<uint32_t> udp_channel_generation_{0};
    
    std::unique_ptr<AudioStreamPacket> last_valid_packet_;
    std::mutex last_packet_mutex_;

    bool StartMqttClient(bool report_error=false);
    void ResetMqttClient(const char* reason);
    void ScheduleReconnect();
    bool CloseAudioChannelInternal(const char* reason, bool notify_application);
    void ParseServerHello(const cJSON* root);
    std::string DecodeHexString(const std::string& hex_string);

    bool SendText(const std::string& text) override;
    std::string GetHelloMessage();

    bool SendEmptyAudioPacket();

    void HandlePacketLoss(uint32_t lost_packets_count);

};


#endif // MQTT_PROTOCOL_H
