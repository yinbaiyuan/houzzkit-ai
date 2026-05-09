#include "voice_controller.h"

#include "application.h"
#include "board.h"
#include "display.h"
#include "esphome_device.h"
#include "esphome_voice_device.h"
#include "assets/lang_config.h"
#include "audio_service.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <esp_heap_caps.h>
#include <esp_log.h>

#define TAG "VoiceController"

namespace {

const char* const VOICE_STATE_STRINGS[] = {
    "idle",
    "connecting",
    "listening",
    "speaking",
    "audio_testing",
    "invalid_state"
};

const char* VoiceInteractionStateToString(VoiceInteractionState state) {
    auto index = static_cast<int>(state);
    if (index < 0 || index >= static_cast<int>(sizeof(VOICE_STATE_STRINGS) / sizeof(VOICE_STATE_STRINGS[0])) - 1) {
        return VOICE_STATE_STRINGS[sizeof(VOICE_STATE_STRINGS) / sizeof(VOICE_STATE_STRINGS[0]) - 1];
    }
    return VOICE_STATE_STRINGS[index];
}

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

} // namespace

VoiceController::VoiceController()
    : audio_service_(std::make_unique<AudioService>()) {
#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_.store(kAecOnDeviceSide);
#elif CONFIG_USE_SERVER_AEC
    aec_mode_.store(kAecOnServerSide);
#else
    aec_mode_.store(kAecOff);
#endif
}

VoiceController::~VoiceController() = default;

AudioService& VoiceController::GetAudioService() {
    return *audio_service_;
}

void VoiceController::Initialize(Application& app, EventGroupHandle_t event_group) {
    app_ = &app;
    event_group_ = event_group;

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        (void)wake_word;
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        (void)speaking;
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    callbacks.on_playback_end = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_PLAYBACK_END);
    };
    audio_service_->SetCallbacks(callbacks);

    auto codec = Board::GetInstance().GetAudioCodec();
    audio_service_->Initialize(codec);
    Start();
}

void VoiceController::SetupProtocol(Protocol& protocol) {
    protocol_ = &protocol;
    auto codec = Board::GetInstance().GetAudioCodec();

    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (IsSpeaking()) {
            audio_service_->PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec]() {
        auto& board = Board::GetInstance();
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this]() {
        auto& board = Board::GetInstance();
        board.SetPowerSaveMode(true);
        CheckHeapIntegrity("audio_channel_closed_before_reset_uplink");
        audio_service_->ResetUplink();
        CheckHeapIntegrity("audio_channel_closed_after_reset_uplink");
        app_->Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetInteractionState(kVoiceInteractionStateIdle);
        });
    });
}

bool VoiceController::HandleIncomingJson(const cJSON* root, Display* display) {
    auto type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        return false;
    }

    if (strcmp(type->valuestring, "tts") == 0) {
        auto state = cJSON_GetObjectItem(root, "state");
        if (strcmp(state->valuestring, "start") == 0) {
            app_->Schedule([this]() {
                aborted_ = false;
                if (IsIdle() || IsListening()) {
                    SetInteractionState(kVoiceInteractionStateSpeaking);
                }
            });
        } else if (strcmp(state->valuestring, "stop") == 0) {
            app_->Schedule([this]() {
                if (audio_service_->IsAudioPlaybackQueueEmpty()) {
                    xEventGroupSetBits(event_group_, MAIN_EVENT_PLAYBACK_END);
                } else {
                    audio_service_->SetWaitTtsStop();
                }
            });
        } else if (strcmp(state->valuestring, "sentence_start") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, "<< %s", text->valuestring);
                app_->Schedule([display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("assistant", message.c_str());
                });
            }
        } else if (strcmp(state->valuestring, "play_start") == 0) {
            app_->Schedule([this]() {
                SetInteractionState(kVoiceInteractionStateSpeaking);
            });
        } else if (strcmp(state->valuestring, "play_stop") == 0) {
            app_->Schedule([this, display]() {
                SetInteractionState(kVoiceInteractionStateIdle);
                display->SetChatMessage("system", "");
                if (protocol_) {
                    protocol_->CloseAudioChannel();
                }
            });
        } else if (strcmp(state->valuestring, "listen_start") == 0) {
            app_->Schedule([this, display]() {
                SetListeningMode(GetPreferredChatListeningMode());
                display->SetChatMessage("system", "");
            });
        }
        return true;
    }

    if (strcmp(type->valuestring, "stt") == 0) {
        auto text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            ESP_LOGI(TAG, ">> %s", text->valuestring);
            app_->Schedule([display, message = std::string(text->valuestring)]() {
                display->SetChatMessage("user", message.c_str());
            });
        }
        return true;
    }

    if (strcmp(type->valuestring, "llm") == 0) {
        auto emotion = cJSON_GetObjectItem(root, "emotion");
        if (cJSON_IsString(emotion)) {
            app_->Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                display->SetEmotion(emotion_str.c_str());
            });
        }
        return true;
    }

    return false;
}

EventBits_t VoiceController::GetPreScheduleEventMask() const {
    return MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE;
}

EventBits_t VoiceController::GetPostScheduleEventMask() const {
    return MAIN_EVENT_PLAYBACK_END;
}

void VoiceController::HandlePreScheduleEventBits(EventBits_t bits) {
    HandleEventBits(bits & GetPreScheduleEventMask());
}

void VoiceController::HandlePostScheduleEventBits(EventBits_t bits) {
    HandleEventBits(bits & GetPostScheduleEventMask());
}

void VoiceController::HandleEventBits(EventBits_t bits) {
    if (bits & MAIN_EVENT_SEND_AUDIO) {
        static int64_t last_uplink_log_us = 0;
        static uint32_t uplink_packets_since_log = 0;
        static uint32_t uplink_failures_since_log = 0;
        while (auto packet = audio_service_->PopPacketFromSendQueue()) {
            if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                uplink_failures_since_log++;
                protocol_->CloseAudioChannel();
                break;
            }
            uplink_packets_since_log++;
        }
        auto now_us = esp_timer_get_time();
        if (uplink_packets_since_log > 0 && now_us - last_uplink_log_us >= 2000000) {
            ESP_LOGI(TAG, "Uplink audio packets: sent=%lu failures=%lu voice_state=%s voice_detected=%d",
                static_cast<unsigned long>(uplink_packets_since_log),
                static_cast<unsigned long>(uplink_failures_since_log),
                VoiceInteractionStateToString(interaction_state_),
                audio_service_->IsVoiceDetected() ? 1 : 0);
            uplink_packets_since_log = 0;
            uplink_failures_since_log = 0;
            last_uplink_log_us = now_us;
        }
    }

    if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
        OnWakeWordDetected();
    }

    if (bits & MAIN_EVENT_VAD_CHANGE) {
        ESP_LOGI(TAG, "VAD: %s voice_state=%s mode=%s",
            audio_service_->IsVoiceDetected() ? "speech" : "silence",
            VoiceInteractionStateToString(interaction_state_),
            ListeningModeToString(listening_mode_));
        if (IsListening()) {
            auto led = Board::GetInstance().GetLed();
            led->OnStateChanged();
        }
    }

    if (bits & MAIN_EVENT_PLAYBACK_END) {
        if (IsSpeaking()) {
            if (listening_mode_ == kListeningModeManualStop) {
                SetInteractionState(kVoiceInteractionStateIdle);
            } else if (ESPHomeVoiceDevice::GetInstance().continuousDialogue()) {
                ESP_LOGI(TAG, "Continuous dialogue playback ended, return to listening");
                auto display = Board::GetInstance().GetDisplay();
                display->SetChatMessage("system", "");
                SetListeningMode(GetPreferredChatListeningMode());
            } else {
                auto display = Board::GetInstance().GetDisplay();
                display->SetChatMessage("system", "");
                SetInteractionState(kVoiceInteractionStateIdle);
            }
        }
    }
}

void VoiceController::SetInteractionState(VoiceInteractionState state) {
    if (interaction_state_ == state) {
        return;
    }

    auto previous_state = interaction_state_;
    interaction_state_ = state;
    ESP_LOGI(TAG, "VOICE STATE: %s", VoiceInteractionStateToString(interaction_state_));
    Board::GetInstance().GetLed()->OnStateChanged();
    OnInteractionStateChanged(previous_state, state);
}

void VoiceController::OnInteractionStateChanged(VoiceInteractionState previous_state, VoiceInteractionState state) {
    auto display = Board::GetInstance().GetDisplay();
    switch (state) {
    case kVoiceInteractionStateIdle:
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        if (ESPHomeDevice::GetInstance().idleScreenOff()) {
            display->setDisplayOnOff(false);
        }
        audio_service_->EnableVoiceProcessing(false);
        audio_service_->EnableWakeWordDetection(true);
        break;
    case kVoiceInteractionStateConnecting:
        display->SetStatus(Lang::Strings::CONNECTING);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
        ESPHomeVoiceDevice::GetInstance().updateOutputVolume();
        break;
    case kVoiceInteractionStateListening:
        display->SetStatus(Lang::Strings::LISTENING);
        display->SetEmotion("wakeup");
        display->setDisplayOnOff(true);
        ESP_LOGI(TAG, "Listening mode: %s, previous_voice_state: %s, audio_processor_running: %d",
            ListeningModeToString(listening_mode_),
            VoiceInteractionStateToString(previous_state),
            audio_service_->IsAudioProcessorRunning() ? 1 : 0);
        protocol_->SendStartListening(listening_mode_);
        audio_service_->EnableWakeWordDetection(false);

        if (!audio_service_->IsAudioProcessorRunning()) {
            if (previous_state == kVoiceInteractionStateSpeaking) {
                CheckHeapIntegrity("enter_listening_before_reset_uplink");
                audio_service_->ResetUplink();
                CheckHeapIntegrity("enter_listening_after_reset_uplink");
            }
            audio_service_->EnableVoiceProcessing(true);
            CheckHeapIntegrity("enter_listening_after_voice_processing");
        }
        break;
    case kVoiceInteractionStateSpeaking:
    {
        display->SetStatus(Lang::Strings::SPEAKING);
        const bool allow_speaking_wake = SupportsRealtimeListening() &&
            listening_mode_ == kListeningModeRealtime &&
            audio_service_->IsAfeWakeWord();
        if (allow_speaking_wake) {
            audio_service_->EnableWakeWordDetection(true);
        } else {
            audio_service_->EnableVoiceProcessing(false);
            audio_service_->EnableWakeWordDetection(false);
        }
        audio_service_->ResetDecoder();
        CheckHeapIntegrity("enter_speaking_after_reset_decoder");
        break;
    }
    case kVoiceInteractionStateAudioTesting:
        break;
    default:
        break;
    }
}

void VoiceController::Start() {
    audio_service_->Start();
    if (IsIdle()) {
        audio_service_->EnableVoiceProcessing(false);
        audio_service_->EnableWakeWordDetection(true);
    }
}

void VoiceController::Stop() {
    audio_service_->Stop();
}

void VoiceController::PrepareForReboot() {
    Stop();
}

void VoiceController::PrepareForFirmwareUpgrade() {
    Stop();
}

void VoiceController::RecoverAfterFirmwareUpgradeFailure() {
    Start();
}

bool VoiceController::CanEnterSleepMode() {
    return IsIdle() && audio_service_->IsIdle();
}

bool VoiceController::IsVoiceDetected() const {
    return audio_service_->IsVoiceDetected();
}

bool VoiceController::RunOnMainLoop(std::function<void()> callback) {
    if (app_ == nullptr) {
        ESP_LOGW(TAG, "VoiceController is not initialized");
        return false;
    }

    if (app_->IsMainEventLoopTask()) {
        return true;
    }

    app_->Schedule(std::move(callback));
    return false;
}

void VoiceController::ToggleChatState() {
    if (!RunOnMainLoop([this]() { ToggleChatStateOnMain(); })) {
        return;
    }
    ToggleChatStateOnMain();
}

void VoiceController::ToggleChatStateOnMain() {
    auto device_state = app_->GetDeviceState();
    if (device_state == kDeviceStateActivating) {
        app_->EnterRunning();
        return;
    } else if (device_state == kDeviceStateWifiConfiguring && interaction_state_ != kVoiceInteractionStateAudioTesting) {
        audio_service_->EnableAudioTesting(true);
        SetInteractionState(kVoiceInteractionStateAudioTesting);
        return;
    } else if (interaction_state_ == kVoiceInteractionStateAudioTesting) {
        audio_service_->EnableAudioTesting(false);
        SetInteractionState(kVoiceInteractionStateIdle);
        return;
    } else if (device_state != kDeviceStateRunning) {
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (IsIdle()) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetInteractionState(kVoiceInteractionStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(GetPreferredChatListeningMode());
    } else if (IsSpeaking()) {
        AbortSpeakingOnMain(kAbortReasonNone);
        auto display = Board::GetInstance().GetDisplay();
        display->SetChatMessage("system", "");
        SetListeningMode(GetPreferredChatListeningMode());
    } else if (IsListening()) {
        protocol_->CloseAudioChannel();
    }
}

void VoiceController::StartListening() {
    if (!RunOnMainLoop([this]() { StartListeningOnMain(); })) {
        return;
    }
    StartListeningOnMain();
}

void VoiceController::StartListeningOnMain() {
    auto device_state = app_->GetDeviceState();
    if (device_state == kDeviceStateActivating) {
        app_->EnterRunning();
        return;
    } else if (device_state == kDeviceStateWifiConfiguring) {
        audio_service_->EnableAudioTesting(true);
        SetInteractionState(kVoiceInteractionStateAudioTesting);
        return;
    } else if (device_state != kDeviceStateRunning) {
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (IsIdle()) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetInteractionState(kVoiceInteractionStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(kListeningModeManualStop);
    } else if (IsSpeaking()) {
        AbortSpeakingOnMain(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void VoiceController::StopListening() {
    if (!RunOnMainLoop([this]() { StopListeningOnMain(); })) {
        return;
    }
    StopListeningOnMain();
}

void VoiceController::StopListeningOnMain() {
    if (interaction_state_ == kVoiceInteractionStateAudioTesting) {
        audio_service_->EnableAudioTesting(false);
        SetInteractionState(kVoiceInteractionStateIdle);
        return;
    }

    const std::array<VoiceInteractionState, 3> valid_states = {
        kVoiceInteractionStateListening,
        kVoiceInteractionStateSpeaking,
        kVoiceInteractionStateIdle,
    };
    if (std::find(valid_states.begin(), valid_states.end(), interaction_state_) == valid_states.end()) {
        return;
    }

    if (IsListening()) {
        protocol_->SendStopListening();
        SetInteractionState(kVoiceInteractionStateIdle);
    }
}

void VoiceController::OnWakeWordDetected() {
    if (!protocol_) {
        audio_service_->EnableWakeWordDetection(true);
        return;
    }

    auto device_state = app_->GetDeviceState();
    if (device_state != kDeviceStateRunning && device_state != kDeviceStateActivating) {
        audio_service_->EnableWakeWordDetection(true);
        return;
    }

    if (!ESPHomeVoiceDevice::GetInstance().micEnabled()) {
        audio_service_->EnableWakeWordDetection(true);
        return;
    }

    if (IsIdle()) {
        audio_service_->EncodeWakeWord();
        if (!protocol_->IsAudioChannelOpened()) {
            ESP_LOGI(TAG, "Wake word trigger is opening audio channel from idle state");
            SetInteractionState(kVoiceInteractionStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_->EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_->GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
        audio_service_->PlaySound(Lang::Sounds::OGG_POPUP);
        vTaskDelay(pdMS_TO_TICKS(500));
        SetListeningMode(GetPreferredChatListeningMode());
    } else if (IsSpeaking()) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state == kDeviceStateActivating) {
        app_->EnterRunning();
    }
}

AecMode VoiceController::GetEffectiveAecMode() const {
    switch (aec_mode_.load()) {
    case kAecOnServerSide:
        return kAecOnServerSide;
    case kAecOnDeviceSide:
        return audio_service_->SupportsDeviceAec() ? kAecOnDeviceSide : kAecOff;
    case kAecOff:
    default:
        return kAecOff;
    }
}

bool VoiceController::SupportsRealtimeListening() const {
    return GetEffectiveAecMode() != kAecOff;
}

ListeningMode VoiceController::GetPreferredChatListeningMode() const {
    return SupportsRealtimeListening() ? kListeningModeRealtime : kListeningModeAutoStop;
}

void VoiceController::AbortSpeaking(AbortReason reason) {
    if (!RunOnMainLoop([this, reason]() { AbortSpeakingOnMain(reason); })) {
        return;
    }
    AbortSpeakingOnMain(reason);
}

void VoiceController::AbortSpeakingOnMain(AbortReason reason) {
    const char* reason_str = reason == kAbortReasonWakeWordDetected ? "wake_word_detected" : "none";
    ESP_LOGI(TAG, "Abort speaking: reason=%s", reason_str);
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void VoiceController::SetListeningMode(ListeningMode mode) {
    if (mode == kListeningModeRealtime && !SupportsRealtimeListening()) {
        mode = kListeningModeAutoStop;
    }
    listening_mode_ = mode;
    SetInteractionState(kVoiceInteractionStateListening);
}

void VoiceController::playVoiceText(const std::string& text) {
    if (!RunOnMainLoop([this, text]() { playVoiceTextOnMain(text); })) {
        return;
    }
    playVoiceTextOnMain(text);
}

void VoiceController::playVoiceTextOnMain(const std::string& text) {
    audio_service_->EnableWakeWordDetection(false);
    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        audio_service_->EnableWakeWordDetection(true);
        return;
    }
    if (!protocol_->IsAudioChannelOpened()) {
        SetInteractionState(kVoiceInteractionStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            audio_service_->EnableWakeWordDetection(true);
            return;
        }
    }
    protocol_->sendPlayVoiceText(text);
}

void VoiceController::executeCommandText(const std::string& command) {
    if (!RunOnMainLoop([this, command]() { executeCommandTextOnMain(command); })) {
        return;
    }
    executeCommandTextOnMain(command);
}

void VoiceController::executeCommandTextOnMain(const std::string& command) {
    audio_service_->EnableWakeWordDetection(false);
    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        audio_service_->EnableWakeWordDetection(true);
        return;
    }
    if (!protocol_->IsAudioChannelOpened()) {
        SetInteractionState(kVoiceInteractionStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            audio_service_->EnableWakeWordDetection(true);
            return;
        }
    }
    protocol_->sendExecuteCommandText(command);
}

void VoiceController::askAndExecuteCommandText(const std::string& command) {
    if (!RunOnMainLoop([this, command]() { askAndExecuteCommandTextOnMain(command); })) {
        return;
    }
    askAndExecuteCommandTextOnMain(command);
}

void VoiceController::askAndExecuteCommandTextOnMain(const std::string& command) {
    audio_service_->EnableWakeWordDetection(false);
    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        audio_service_->EnableWakeWordDetection(true);
        return;
    }
    if (!protocol_->IsAudioChannelOpened()) {
        SetInteractionState(kVoiceInteractionStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            audio_service_->EnableWakeWordDetection(true);
            return;
        }
    }
    protocol_->sendAskAndExecuteCommandText(command);
}

void VoiceController::WakeWordInvoke(const std::string& wake_word) {
    if (!RunOnMainLoop([this, wake_word]() { WakeWordInvokeOnMain(wake_word); })) {
        return;
    }
    WakeWordInvokeOnMain(wake_word);
}

void VoiceController::WakeWordInvokeOnMain(const std::string& wake_word) {
    if (IsIdle()) {
        ToggleChatStateOnMain();
        if (protocol_) {
            protocol_->SendWakeWordDetected(wake_word);
        }
    } else if (IsSpeaking()) {
        AbortSpeakingOnMain(kAbortReasonNone);
    } else if (IsListening()) {
        if (protocol_) {
            protocol_->CloseAudioChannel();
        }
    }
}

void VoiceController::SetAecMode(AecMode mode) {
    if (!RunOnMainLoop([this, mode]() { SetAecModeOnMain(mode); })) {
        return;
    }
    SetAecModeOnMain(mode);
}

void VoiceController::SetAecModeOnMain(AecMode mode) {
    aec_mode_.store(mode);
    auto display = Board::GetInstance().GetDisplay();
    switch (mode) {
    case kAecOff:
        audio_service_->EnableDeviceAec(false);
        display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
        break;
    case kAecOnServerSide:
        audio_service_->EnableDeviceAec(false);
        display->ShowNotification(Lang::Strings::RTC_MODE_ON);
        break;
    case kAecOnDeviceSide:
        if (audio_service_->SupportsDeviceAec()) {
            audio_service_->EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
        } else {
            ESP_LOGW(TAG, "Device AEC is unavailable on the current audio path, falling back to half duplex listening");
            audio_service_->EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
        }
        break;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
}

void VoiceController::PlaySound(const std::string_view& sound) {
    std::string sound_copy(sound);
    if (!RunOnMainLoop([this, sound_copy]() { PlaySoundOnMain(sound_copy); })) {
        return;
    }
    PlaySoundOnMain(sound_copy);
}

void VoiceController::PlaySoundOnMain(const std::string& sound) {
    audio_service_->PlaySound(sound);
}
