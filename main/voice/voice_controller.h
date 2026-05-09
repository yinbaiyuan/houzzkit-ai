#ifndef VOICE_CONTROLLER_H
#define VOICE_CONTROLLER_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "device_state.h"
#include "protocol.h"

#define MAIN_EVENT_SEND_AUDIO (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED (1 << 2)
#define MAIN_EVENT_VAD_CHANGE (1 << 3)
#define MAIN_EVENT_PLAYBACK_END (1 << 8)

class Application;
class AudioService;
class Display;

enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

#if CONFIG_USE_VOICE_DIALOGUE

class VoiceController {
public:
    VoiceController();
    ~VoiceController();

    void Initialize(Application& app, EventGroupHandle_t event_group);
    void SetupProtocol(Protocol& protocol);
    bool HandleIncomingJson(const cJSON* root, Display* display);
    EventBits_t GetPreScheduleEventMask() const;
    EventBits_t GetPostScheduleEventMask() const;
    void HandlePreScheduleEventBits(EventBits_t bits);
    void HandlePostScheduleEventBits(EventBits_t bits);

    void Start();
    void Stop();
    void PrepareForReboot();
    void PrepareForFirmwareUpgrade();
    void RecoverAfterFirmwareUpgradeFailure();
    bool CanEnterSleepMode();
    bool IsVoiceDetected() const;
    VoiceInteractionState GetInteractionState() const { return interaction_state_; }
    bool IsIdle() const { return interaction_state_ == kVoiceInteractionStateIdle; }
    bool IsListening() const { return interaction_state_ == kVoiceInteractionStateListening; }
    bool IsSpeaking() const { return interaction_state_ == kVoiceInteractionStateSpeaking; }

    void AbortSpeaking(AbortReason reason);
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void WakeWordInvoke(const std::string& wake_word);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_.load(); }
    void PlaySound(const std::string_view& sound);
    AudioService& GetAudioService();

    void playVoiceText(const std::string& text);
    void executeCommandText(const std::string& command);
    void askAndExecuteCommandText(const std::string& command);

private:
    Application* app_ = nullptr;
    Protocol* protocol_ = nullptr;
    EventGroupHandle_t event_group_ = nullptr;
    std::unique_ptr<AudioService> audio_service_;
    VoiceInteractionState interaction_state_ = kVoiceInteractionStateIdle;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    std::atomic<AecMode> aec_mode_ { kAecOff };
    bool aborted_ = false;

    bool RunOnMainLoop(std::function<void()> callback);
    void AbortSpeakingOnMain(AbortReason reason);
    void ToggleChatStateOnMain();
    void StartListeningOnMain();
    void StopListeningOnMain();
    void WakeWordInvokeOnMain(const std::string& wake_word);
    void SetAecModeOnMain(AecMode mode);
    void PlaySoundOnMain(const std::string& sound);
    void playVoiceTextOnMain(const std::string& text);
    void executeCommandTextOnMain(const std::string& command);
    void askAndExecuteCommandTextOnMain(const std::string& command);
    void HandleEventBits(EventBits_t bits);
    void OnWakeWordDetected();
    void SetInteractionState(VoiceInteractionState state);
    void OnInteractionStateChanged(VoiceInteractionState previous_state, VoiceInteractionState state);
    void SetListeningMode(ListeningMode mode);
    AecMode GetEffectiveAecMode() const;
    bool SupportsRealtimeListening() const;
    ListeningMode GetPreferredChatListeningMode() const;
};
#else
class VoiceController {
public:
    EventBits_t GetPreScheduleEventMask() const { return 0; }
    EventBits_t GetPostScheduleEventMask() const { return 0; }
    void HandlePreScheduleEventBits(EventBits_t bits) { (void)bits; }
    void HandlePostScheduleEventBits(EventBits_t bits) { (void)bits; }
    void ToggleChatState() {}
    void StartListening() {}
    void StopListening() {}
    void WakeWordInvoke(const std::string& wake_word) { (void)wake_word; }
    void SetAecMode(AecMode mode) { (void)mode; }
    AecMode GetAecMode() const { return kAecOff; }
    void PlaySound(const std::string_view& sound) { (void)sound; }
    bool IsVoiceDetected() const { return false; }
    bool CanEnterSleepMode() const { return true; }
    VoiceInteractionState GetInteractionState() const { return kVoiceInteractionStateIdle; }
    bool IsIdle() const { return true; }
    bool IsListening() const { return false; }
    bool IsSpeaking() const { return false; }
    void PrepareForReboot() {}
    void PrepareForFirmwareUpgrade() {}
    void RecoverAfterFirmwareUpgradeFailure() {}
    void playVoiceText(const std::string& text) { (void)text; }
    void executeCommandText(const std::string& command) { (void)command; }
    void askAndExecuteCommandText(const std::string& command) { (void)command; }
};
#endif

#endif // VOICE_CONTROLLER_H
