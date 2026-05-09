#ifndef AUDIO_SERVICE_H
#define AUDIO_SERVICE_H

#include <memory>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>
#include <model_path.h>

#include <opus_encoder.h>
#include <opus_resampler.h>

#include "audio_codec.h"
#include "audio_processor.h"
#include "opus_stream_decoder.h"
#include "processors/audio_debugger.h"
#include "wake_word.h"
#include "protocol.h"


/*
 * There are two types of audio data flow:
 * 1. (MIC) -> [Processors] -> {Encode Queue} -> [Opus Encoder] -> {Send Queue} -> (Server)
 * 2. (Server) -> {Decode Queue} -> [Opus Decoder] -> {Playback Queue} -> (Speaker)
 *
 * We use one task for MIC / Speaker / Processors, and one task for Opus Encoder / Opus Decoder.
 * 
 * Decode Queue and Send Queue are the main queues, because Opus packets are quite smaller than PCM packets.
 * 
 */

#define OPUS_FRAME_DURATION_MS 60
#define UDP_DOWNLINK_TARGET_FRAME_DURATION_MS 20
#define DOWNLINK_BASE_TARGET_JITTER_MS 200
#define DOWNLINK_WEAK_TARGET_JITTER_MS 240
#define DOWNLINK_STRONG_TARGET_JITTER_MS 280
#define DOWNLINK_MAX_JITTER_BUFFER_MS 500
#define DOWNLINK_RECOVERY_MIN_PLAYBACK_QUEUE_PACKETS 2
#define DOWNLINK_RESYNC_COOLDOWN_MS 800
#define DOWNLINK_RESYNC_TRIM_THRESHOLD 20
#define DOWNLINK_RESYNC_PLC_THRESHOLD 30
#define DOWNLINK_HARD_ARRIVAL_MS 500
#define MAX_ENCODE_TASKS_IN_QUEUE 2
#define MAX_PLAYBACK_TASKS_IN_QUEUE 6
#define MAX_DECODE_QUEUE_AUDIO_MS 1000
#define MAX_SEND_PACKETS_IN_QUEUE (2400 / OPUS_FRAME_DURATION_MS)
#define AUDIO_TESTING_MAX_DURATION_MS 10000
#define MAX_TIMESTAMPS_IN_QUEUE 3

#define AUDIO_POWER_TIMEOUT_MS 15000
#define AUDIO_POWER_CHECK_INTERVAL_MS 1000


#define AS_EVENT_AUDIO_TESTING_RUNNING      (1 << 0)
#define AS_EVENT_WAKE_WORD_RUNNING          (1 << 1)
#define AS_EVENT_AUDIO_PROCESSOR_RUNNING    (1 << 2)
#define AS_EVENT_PLAYBACK_NOT_EMPTY         (1 << 3)

struct AudioServiceCallbacks {
    std::function<void(void)> on_send_queue_available;
    std::function<void(const std::string&)> on_wake_word_detected;
    std::function<void(bool)> on_vad_change;
    std::function<void(void)> on_audio_testing_queue_full;
    std::function<void(void)> on_playback_end;
};


enum AudioTaskType {
    kAudioTaskTypeEncodeToSendQueue,
    kAudioTaskTypeEncodeToTestingQueue,
    kAudioTaskTypeDecodeToPlaybackQueue,
};

struct AudioTask {
    AudioTaskType type;
    std::vector<int16_t> pcm;
    uint32_t timestamp;
    uint32_t sequence = 0;
    int frame_duration = 0;
    bool is_udp_downlink = false;
    uint32_t downlink_generation = 0;
    uint8_t downlink_decode_mode = 0;
    uint32_t downlink_loss_count_before_recovery = 0;
};

struct DebugStatistics {
    uint32_t input_count = 0;
    uint32_t decode_count = 0;
    uint32_t encode_count = 0;
    uint32_t playback_count = 0;
};

enum class DownlinkDecodeMode {
    kNormal,
    kFec,
    kPlc,
};

struct DownlinkDecodeAction {
    DownlinkDecodeMode mode = DownlinkDecodeMode::kNormal;
    int sample_rate = 0;
    int frame_duration = 0;
    uint32_t timestamp = 0;
    uint32_t sequence = 0;
    uint32_t generation = 0;
    uint32_t loss_count_before_recovery = 0;
    bool recovery_frame = false;
    std::vector<uint8_t> payload;
};

struct DownlinkDebugStatistics {
    uint32_t normal_packets = 0;
    uint32_t fec_packets = 0;
    uint32_t plc_packets = 0;
    uint32_t starvation_plc_packets = 0;
    uint32_t late_packets = 0;
    uint32_t duplicate_packets = 0;
    uint32_t trimmed_packets = 0;
    uint32_t sequence_gap_events = 0;
    uint32_t max_sequence_gap = 0;
    uint32_t max_arrival_interval_ms = 0;
    uint32_t max_jitter_buffer_packets = 0;
    uint32_t max_decode_queue_audio_ms = 0;
    uint32_t max_playback_queue_packets = 0;
    uint32_t boundary_jump_events = 0;
    int32_t max_boundary_delta = 0;
    uint32_t clipping_frames = 0;
    int32_t max_pcm_peak = 0;
    uint32_t slow_output_events = 0;
    uint32_t max_output_write_ms = 0;
    uint32_t output_gap_events = 0;
    uint32_t max_output_gap_ms = 0;
    uint32_t generation_mismatch_events = 0;
    uint32_t recovery_waits = 0;
    uint32_t max_recovery_wait_ms = 0;
    uint32_t resync_events = 0;
    uint32_t resync_skip_packets = 0;
    uint32_t resync_dropped_playback_packets = 0;
    uint32_t resync_dropped_jitter_packets = 0;
};

class AudioService {
public:
    AudioService();
    ~AudioService();

    void Initialize(AudioCodec* codec);
    void Start();
    void Stop();
    void EncodeWakeWord();
    std::unique_ptr<AudioStreamPacket> PopWakeWordPacket();
    const std::string& GetLastWakeWord() const;
    bool IsVoiceDetected() const { return voice_detected_; }
    bool IsIdle();
    bool IsWakeWordRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_WAKE_WORD_RUNNING; }
    bool IsAudioProcessorRunning() const { return xEventGroupGetBits(event_group_) & AS_EVENT_AUDIO_PROCESSOR_RUNNING; }
    bool IsAfeWakeWord();

    void EnableWakeWordDetection(bool enable);
    void EnableVoiceProcessing(bool enable);
    void EnableAudioTesting(bool enable);
    void EnableDeviceAec(bool enable);

    void SetCallbacks(AudioServiceCallbacks& callbacks);

    bool PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait = false);
    std::unique_ptr<AudioStreamPacket> PopPacketFromSendQueue();
    void PlaySound(const std::string_view& sound);
    bool ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples);
    void ResetDecoder();
    void SetModelsList(srmodel_list_t* models_list);
    void ResetUplink();
    bool SupportsDeviceAec() const;

    void SetWaitTtsStop() { wait_tts_stop_ = true; }
    bool IsAudioPlaybackQueueEmpty();

private:
    AudioCodec* codec_ = nullptr;
    AudioServiceCallbacks callbacks_;
    std::unique_ptr<AudioProcessor> audio_processor_;
    std::unique_ptr<WakeWord> wake_word_;
    std::unique_ptr<AudioDebugger> audio_debugger_;
    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusStreamDecoder> downlink_decoder_;
    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;
    DebugStatistics debug_statistics_;
    srmodel_list_t* models_list_ = nullptr;

    EventGroupHandle_t event_group_;

    // Audio encode / decode
    TaskHandle_t audio_input_task_handle_ = nullptr;
    TaskHandle_t audio_output_task_handle_ = nullptr;
    TaskHandle_t opus_codec_task_handle_ = nullptr;
    std::mutex audio_queue_mutex_;
    std::condition_variable audio_queue_cv_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_decode_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> passthrough_decode_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_send_queue_;
    std::deque<std::unique_ptr<AudioStreamPacket>> audio_testing_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_encode_queue_;
    std::deque<std::unique_ptr<AudioTask>> audio_playback_queue_;
    std::map<uint32_t, std::unique_ptr<AudioStreamPacket>> downlink_jitter_buffer_;
    // For server AEC
    std::deque<uint32_t> timestamp_queue_;
    DownlinkDebugStatistics downlink_debug_statistics_;
    size_t queued_decode_audio_ms_ = 0;
    uint32_t expected_downlink_sequence_ = 0;
    uint32_t downlink_consecutive_loss_count_ = 0;
    uint32_t downlink_consecutive_plc_count_ = 0;

    bool wake_word_initialized_ = false;
    bool wake_word_detection_requested_ = false;
    bool audio_processor_initialized_ = false;
    bool voice_detected_ = false;
    bool service_stopped_ = true;
    bool audio_input_need_warmup_ = false;
    bool wait_tts_stop_ = false;
    bool downlink_playback_started_ = false;
    bool downlink_output_active_ = false;

    esp_timer_handle_t audio_power_timer_ = nullptr;
    std::chrono::steady_clock::time_point last_input_time_;
    std::chrono::steady_clock::time_point last_output_time_;

    void AudioInputTask();
    void AudioOutputTask();
    void OpusCodecTask();
    void PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm);
    void ResetUplinkLocked();
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    size_t GetPacketDurationMs(const AudioStreamPacket& packet) const;
    size_t GetTargetJitterBufferPackets(int frame_duration_ms) const;
    size_t GetMaxJitterBufferPackets(int frame_duration_ms) const;
    void QueueIncomingPacketLocked(std::unique_ptr<AudioStreamPacket> packet);
    void MaybeStartDownlinkPlaybackLocked();
    void TrimJitterBufferLocked();
    bool CanProduceJitterPlaybackLocked();
    bool PrepareDownlinkDecodeActionLocked(DownlinkDecodeAction& action);
    bool ExecuteDownlinkDecodeAction(const DownlinkDecodeAction& action, std::unique_ptr<AudioTask>& task);
    bool DecodePassthroughPacket(std::unique_ptr<AudioStreamPacket> packet, std::unique_ptr<AudioTask>& task);
    bool HasPendingDownlinkPlaybackLocked() const;
    void ObserveDownlinkPacketLocked(const AudioStreamPacket& packet);
    void ObserveDecodedDownlinkPcmLocked(const AudioTask& task);
    void ObserveDownlinkOutputLocked(const AudioTask& task, std::chrono::steady_clock::time_point output_start, uint32_t write_ms);
    void ApplyDownlinkRecoveryDeclick(const DownlinkDecodeAction& action, std::vector<int16_t>& pcm);
    bool ShouldWaitForDownlinkRecoveryLocked(int frame_duration_ms);
    void ClearDownlinkRecoveryDeadlineLocked();
    bool MaybeResyncDownlinkAfterHardFailureLocked(size_t max_packets);
    uint32_t AdaptDownlinkTargetAfterSessionLocked(const DownlinkDebugStatistics& stats) const;
    void ResetDecoderStateLocked();
    void CheckAndUpdateAudioPowerState();

    int downlink_last_sample_rate_ = 0;
    int downlink_last_frame_duration_ = UDP_DOWNLINK_TARGET_FRAME_DURATION_MS;
    uint32_t downlink_current_target_ms_ = DOWNLINK_BASE_TARGET_JITTER_MS;
    uint32_t downlink_next_target_ms_ = DOWNLINK_BASE_TARGET_JITTER_MS;
    bool downlink_recovery_deadline_active_ = false;
    uint32_t downlink_recovery_deadline_sequence_ = 0;
    std::chrono::steady_clock::time_point downlink_recovery_deadline_;
    bool downlink_last_resync_time_valid_ = false;
    std::chrono::steady_clock::time_point downlink_last_resync_time_;
    uint32_t downlink_resync_baseline_output_gaps_ = 0;
    uint32_t downlink_resync_baseline_trimmed_packets_ = 0;
    uint32_t downlink_resync_baseline_plc_packets_ = 0;
    bool downlink_recovery_boundary_pending_ = false;
    bool downlink_recovery_tail_valid_ = false;
    int16_t downlink_recovery_tail_sample_ = 0;
    uint32_t downlink_generation_ = 0;
    bool downlink_last_arrival_time_valid_ = false;
    std::chrono::steady_clock::time_point downlink_last_arrival_time_;
    bool downlink_last_arrival_sequence_valid_ = false;
    uint32_t downlink_last_arrival_sequence_ = 0;
    bool downlink_quality_tail_valid_ = false;
    int16_t downlink_quality_tail_sample_ = 0;
    bool downlink_last_output_finish_valid_ = false;
    std::chrono::steady_clock::time_point downlink_last_output_finish_;
};

#endif
