#include "audio_service.h"
#include <algorithm>
#include <esp_log.h>
#include <cstring>

#if CONFIG_USE_AUDIO_PROCESSOR
#include "processors/afe_audio_processor.h"
#else
#include "processors/no_audio_processor.h"
#endif

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#include "wake_words/afe_wake_word.h"
#include "wake_words/custom_wake_word.h"
#else
#include "wake_words/esp_wake_word.h"
#endif

#define TAG "AudioService"

namespace {

constexpr int kDownlinkDeclickDurationMs = 3;
constexpr int kDownlinkBoundaryJumpWarning = 12000;
constexpr int kDownlinkClippingPeakWarning = 32000;
constexpr uint32_t kDownlinkOutputGapWarningMs = 120;
constexpr uint32_t kDownlinkMinSlowOutputWarningMs = 50;

int32_t AbsInt32(int32_t value) {
    return value < 0 ? -value : value;
}

uint32_t PromoteDownlinkTargetMs(uint32_t target_ms) {
    if (target_ms < DOWNLINK_WEAK_TARGET_JITTER_MS) {
        return DOWNLINK_WEAK_TARGET_JITTER_MS;
    }
    return DOWNLINK_STRONG_TARGET_JITTER_MS;
}

uint32_t DemoteDownlinkTargetMs(uint32_t target_ms) {
    if (target_ms > DOWNLINK_WEAK_TARGET_JITTER_MS) {
        return DOWNLINK_WEAK_TARGET_JITTER_MS;
    }
    return DOWNLINK_BASE_TARGET_JITTER_MS;
}

}  // namespace


AudioService::AudioService() {
    event_group_ = xEventGroupCreate();
}

AudioService::~AudioService() {
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
    }
}


void AudioService::Initialize(AudioCodec* codec) {
    codec_ = codec;
    codec_->Start();

    /* Setup the audio codec */
    downlink_decoder_ = std::make_unique<OpusStreamDecoder>(codec->output_sample_rate(), 1, UDP_DOWNLINK_TARGET_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_->SetComplexity(0);

    if (codec->input_sample_rate() != 16000) {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<NoAudioProcessor>();
#endif

    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue, std::move(data));
    });

    audio_processor_->OnVadStateChange([this](bool speaking) {
        voice_detected_ = speaking;
        if (callbacks_.on_vad_change) {
            callbacks_.on_vad_change(speaking);
        }
    });

    esp_timer_create_args_t audio_power_timer_args = {
        .callback = [](void* arg) {
            AudioService* audio_service = (AudioService*)arg;
            audio_service->CheckAndUpdateAudioPowerState();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "audio_power_timer",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&audio_power_timer_args, &audio_power_timer_);
}

void AudioService::Start() {
    service_stopped_ = false;
    xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING | AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    esp_timer_start_periodic(audio_power_timer_, 1000000);

#if CONFIG_USE_AUDIO_PROCESSOR
    /* Start the audio input task */
    xTaskCreatePinnedToCore([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 3, this, 8, &audio_input_task_handle_, 0);

    /* Start the audio output task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioOutputTask();
        vTaskDelete(NULL);
    }, "audio_output", 2048 * 2, this, 4, &audio_output_task_handle_);
#else
    /* Start the audio input task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioInputTask();
        vTaskDelete(NULL);
    }, "audio_input", 2048 * 2, this, 8, &audio_input_task_handle_);

    /* Start the audio output task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->AudioOutputTask();
        vTaskDelete(NULL);
    }, "audio_output", 2048, this, 4, &audio_output_task_handle_);
#endif

    /* Start the opus codec task */
    xTaskCreate([](void* arg) {
        AudioService* audio_service = (AudioService*)arg;
        audio_service->OpusCodecTask();
        vTaskDelete(NULL);
    }, "opus_codec", 2048 * 13, this, 2, &opus_codec_task_handle_);
}

void AudioService::Stop() {
    esp_timer_stop(audio_power_timer_);
    service_stopped_ = true;
    xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
        AS_EVENT_WAKE_WORD_RUNNING |
        AS_EVENT_AUDIO_PROCESSOR_RUNNING);

    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    ResetUplinkLocked();
    ResetDecoderStateLocked();
    audio_queue_cv_.notify_all();
}

bool AudioService::ReadAudioData(std::vector<int16_t>& data, int sample_rate, int samples) {
    if (!codec_->input_enabled()) {
        esp_timer_stop(audio_power_timer_);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableInput(true);
    }

    if (codec_->input_sample_rate() != sample_rate) {
        data.resize(samples * codec_->input_sample_rate() / sample_rate * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
        if (codec_->input_channels() == 2) {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    } else {
        data.resize(samples * codec_->input_channels());
        if (!codec_->InputData(data)) {
            return false;
        }
    }

    /* Update the last input time */
    last_input_time_ = std::chrono::steady_clock::now();
    debug_statistics_.input_count++;

#if CONFIG_USE_AUDIO_DEBUGGER
    // 音频调试：发送原始音频数据
    if (audio_debugger_ == nullptr) {
        audio_debugger_ = std::make_unique<AudioDebugger>();
    }
    audio_debugger_->Feed(data);
#endif

    return true;
}

void AudioService::AudioInputTask() {
    while (true) {
        EventBits_t bits = xEventGroupWaitBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING |
            AS_EVENT_WAKE_WORD_RUNNING | AS_EVENT_AUDIO_PROCESSOR_RUNNING,
            pdFALSE, pdFALSE, portMAX_DELAY);

        if (service_stopped_) {
            break;
        }
        if (audio_input_need_warmup_) {
            audio_input_need_warmup_ = false;
            vTaskDelay(pdMS_TO_TICKS(120));
            continue;
        }

        /* Used for audio testing in NetworkConfiguring mode by clicking the BOOT button */
        if (bits & AS_EVENT_AUDIO_TESTING_RUNNING) {
            if (audio_testing_queue_.size() >= AUDIO_TESTING_MAX_DURATION_MS / OPUS_FRAME_DURATION_MS) {
                ESP_LOGW(TAG, "Audio testing queue is full, stopping audio testing");
                EnableAudioTesting(false);
                continue;
            }
            std::vector<int16_t> data;
            int samples = OPUS_FRAME_DURATION_MS * 16000 / 1000;
            if (ReadAudioData(data, 16000, samples)) {
                // If input channels is 2, we need to fetch the left channel data
                if (codec_->input_channels() == 2) {
                    auto mono_data = std::vector<int16_t>(data.size() / 2);
                    for (size_t i = 0, j = 0; i < mono_data.size(); ++i, j += 2) {
                        mono_data[i] = data[j];
                    }
                    data = std::move(mono_data);
                }
                PushTaskToEncodeQueue(kAudioTaskTypeEncodeToTestingQueue, std::move(data));
                continue;
            }
        }

        /* Feed the wake word */
        if (bits & AS_EVENT_WAKE_WORD_RUNNING) {
            std::vector<int16_t> data;
            int samples = wake_word_->GetFeedSize();
            if (samples > 0) {
                if (ReadAudioData(data, 16000, samples)) {
                    wake_word_->Feed(data);
                    continue;
                }
            }
        }

        /* Feed the audio processor */
        if (bits & AS_EVENT_AUDIO_PROCESSOR_RUNNING) {
            std::vector<int16_t> data;
            int samples = audio_processor_->GetFeedSize();
            if (samples > 0) {
                if (ReadAudioData(data, 16000, samples)) {
                    audio_processor_->Feed(std::move(data));
                    continue;
                }
            }
        }

        ESP_LOGE(TAG, "Should not be here, bits: %lx", bits);
        break;
    }

    ESP_LOGW(TAG, "Audio input task stopped");
}

void AudioService::AudioOutputTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        audio_queue_cv_.wait(lock, [this]() { return !audio_playback_queue_.empty() || service_stopped_; });
        if (service_stopped_) {
            break;
        }

        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();
        if (task->is_udp_downlink) {
            downlink_output_active_ = true;
        }
        audio_queue_cv_.notify_all();
        lock.unlock();

        if (!codec_->output_enabled()) {
            esp_timer_stop(audio_power_timer_);
            esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
            codec_->EnableOutput(true);
        }
        const auto output_start = std::chrono::steady_clock::now();
        codec_->OutputData(task->pcm);
        const auto output_end = std::chrono::steady_clock::now();
        const auto output_write_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(output_end - output_start).count());

        lock.lock();
        if (task->is_udp_downlink) {
            ObserveDownlinkOutputLocked(*task, output_start, output_write_ms);
            downlink_output_active_ = false;
        }
        audio_queue_cv_.notify_all();
        if (wait_tts_stop_ && !HasPendingDownlinkPlaybackLocked() && callbacks_.on_playback_end) {
            callbacks_.on_playback_end();
            wait_tts_stop_ = false;
        }

        /* Update the last output time */
        last_output_time_ = std::chrono::steady_clock::now();
        debug_statistics_.playback_count++;

#if CONFIG_USE_SERVER_AEC
        /* Record the timestamp for server AEC */
        if (task->timestamp > 0) {
            timestamp_queue_.push_back(task->timestamp);
        }
#endif
    }

    ESP_LOGW(TAG, "Audio output task stopped");
}

void AudioService::OpusCodecTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(audio_queue_mutex_);
        auto has_codec_work = [this]() {
            return service_stopped_ ||
                !audio_decode_queue_.empty() ||
                (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) ||
                (audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE &&
                    (!passthrough_decode_queue_.empty() || CanProduceJitterPlaybackLocked()));
        };
        while (!has_codec_work()) {
            if (downlink_recovery_deadline_active_) {
                audio_queue_cv_.wait_until(lock, downlink_recovery_deadline_);
            } else {
                audio_queue_cv_.wait(lock);
            }
        }
        if (service_stopped_) {
            break;
        }

        if (!audio_decode_queue_.empty()) {
            auto packet = std::move(audio_decode_queue_.front());
            audio_decode_queue_.pop_front();
            queued_decode_audio_ms_ -= std::min(queued_decode_audio_ms_, GetPacketDurationMs(*packet));
            QueueIncomingPacketLocked(std::move(packet));
            audio_queue_cv_.notify_all();
        }

        if (audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE) {
            std::unique_ptr<AudioTask> decoded_task;
            std::unique_ptr<AudioStreamPacket> passthrough_packet;
            DownlinkDecodeAction action;
            bool has_decode_work = false;

            if (!passthrough_decode_queue_.empty()) {
                passthrough_packet = std::move(passthrough_decode_queue_.front());
                passthrough_decode_queue_.pop_front();
                has_decode_work = true;
            } else {
                has_decode_work = PrepareDownlinkDecodeActionLocked(action);
            }

            if (has_decode_work) {
                lock.unlock();

                bool decoded = false;
                if (passthrough_packet != nullptr) {
                    decoded = DecodePassthroughPacket(std::move(passthrough_packet), decoded_task);
                } else {
                    decoded = ExecuteDownlinkDecodeAction(action, decoded_task);
                }

                lock.lock();
                if (service_stopped_) {
                    break;
                }

                if (decoded) {
                    bool generation_mismatch_logged = false;
                    if (decoded_task->is_udp_downlink) {
                        if (decoded_task->downlink_generation != downlink_generation_) {
                            downlink_debug_statistics_.generation_mismatch_events++;
                            generation_mismatch_logged = true;
                            ESP_LOGW(TAG,
                                "Decoded downlink frame crossed reset generation: seq=%lu task_gen=%lu current_gen=%lu mode=%u mismatches=%lu",
                                static_cast<unsigned long>(decoded_task->sequence),
                                static_cast<unsigned long>(decoded_task->downlink_generation),
                                static_cast<unsigned long>(downlink_generation_),
                                static_cast<unsigned>(decoded_task->downlink_decode_mode),
                                static_cast<unsigned long>(downlink_debug_statistics_.generation_mismatch_events));
                        }
                        ObserveDecodedDownlinkPcmLocked(*decoded_task);
                    }
                    audio_queue_cv_.wait(lock, [this]() {
                        return service_stopped_ || audio_playback_queue_.size() < MAX_PLAYBACK_TASKS_IN_QUEUE;
                    });
                    if (service_stopped_) {
                        break;
                    }
                    if (decoded_task->is_udp_downlink &&
                        decoded_task->downlink_generation != downlink_generation_ &&
                        !generation_mismatch_logged) {
                        downlink_debug_statistics_.generation_mismatch_events++;
                        ESP_LOGW(TAG,
                            "Queueing downlink frame after reset generation changed: seq=%lu task_gen=%lu current_gen=%lu mismatches=%lu",
                            static_cast<unsigned long>(decoded_task->sequence),
                            static_cast<unsigned long>(decoded_task->downlink_generation),
                            static_cast<unsigned long>(downlink_generation_),
                            static_cast<unsigned long>(downlink_debug_statistics_.generation_mismatch_events));
                    }
                    audio_playback_queue_.push_back(std::move(decoded_task));
                    if (audio_playback_queue_.size() > downlink_debug_statistics_.max_playback_queue_packets) {
                        downlink_debug_statistics_.max_playback_queue_packets = static_cast<uint32_t>(audio_playback_queue_.size());
                    }
                    audio_queue_cv_.notify_all();
                }
                debug_statistics_.decode_count++;
            }
        }

        /* Encode the audio to send queue */
        if (!audio_encode_queue_.empty() && audio_send_queue_.size() < MAX_SEND_PACKETS_IN_QUEUE) {
            auto task = std::move(audio_encode_queue_.front());
            audio_encode_queue_.pop_front();
            audio_queue_cv_.notify_all();
            lock.unlock();

            auto packet = std::make_unique<AudioStreamPacket>();
            packet->frame_duration = OPUS_FRAME_DURATION_MS;
            packet->sample_rate = 16000;
            packet->timestamp = task->timestamp;
            if (!opus_encoder_->Encode(std::move(task->pcm), packet->payload)) {
                ESP_LOGE(TAG, "Failed to encode audio");
                continue;
            }

            if (task->type == kAudioTaskTypeEncodeToSendQueue) {
                {
                    std::lock_guard<std::mutex> send_lock(audio_queue_mutex_);
                    audio_send_queue_.push_back(std::move(packet));
                }
                if (callbacks_.on_send_queue_available) {
                    callbacks_.on_send_queue_available();
                }
            } else if (task->type == kAudioTaskTypeEncodeToTestingQueue) {
                std::lock_guard<std::mutex> lock(audio_queue_mutex_);
                audio_testing_queue_.push_back(std::move(packet));
            }
            debug_statistics_.encode_count++;
            lock.lock();
        }
    }

    ESP_LOGW(TAG, "Opus codec task stopped");
}

size_t AudioService::GetPacketDurationMs(const AudioStreamPacket& packet) const {
    if (packet.frame_duration > 0) {
        return static_cast<size_t>(packet.frame_duration);
    }
    return UDP_DOWNLINK_TARGET_FRAME_DURATION_MS;
}

size_t AudioService::GetTargetJitterBufferPackets(int frame_duration_ms) const {
    const auto duration_ms = std::max(1, frame_duration_ms);
    return std::max<size_t>(1, (downlink_current_target_ms_ + duration_ms - 1) / duration_ms);
}

size_t AudioService::GetMaxJitterBufferPackets(int frame_duration_ms) const {
    const auto duration_ms = std::max(1, frame_duration_ms);
    return std::max<size_t>(1, (DOWNLINK_MAX_JITTER_BUFFER_MS + duration_ms - 1) / duration_ms);
}

void AudioService::QueueIncomingPacketLocked(std::unique_ptr<AudioStreamPacket> packet) {
    if (packet == nullptr) {
        return;
    }

    if (packet->sequence == 0) {
        passthrough_decode_queue_.push_back(std::move(packet));
        return;
    }

    if (downlink_playback_started_ && packet->sequence < expected_downlink_sequence_) {
        downlink_debug_statistics_.late_packets++;
        const bool should_warn_late = downlink_debug_statistics_.late_packets <= 4 ||
            downlink_debug_statistics_.late_packets % 50 == 0;
        if (should_warn_late) {
            ESP_LOGW(TAG,
                "Dropping late downlink packet: seq=%lu expected=%lu late=%lu duplicate=%lu trimmed=%lu",
                static_cast<unsigned long>(packet->sequence),
                static_cast<unsigned long>(expected_downlink_sequence_),
                static_cast<unsigned long>(downlink_debug_statistics_.late_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.duplicate_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.trimmed_packets));
        } else {
            ESP_LOGD(TAG,
                "Dropping late downlink packet: seq=%lu expected=%lu late=%lu duplicate=%lu trimmed=%lu",
                static_cast<unsigned long>(packet->sequence),
                static_cast<unsigned long>(expected_downlink_sequence_),
                static_cast<unsigned long>(downlink_debug_statistics_.late_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.duplicate_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.trimmed_packets));
        }
        return;
    }

    auto [it, inserted] = downlink_jitter_buffer_.emplace(packet->sequence, nullptr);
    if (!inserted) {
        downlink_debug_statistics_.duplicate_packets++;
        ESP_LOGW(TAG,
            "Dropping duplicate downlink packet: seq=%lu duplicate=%lu late=%lu trimmed=%lu",
            static_cast<unsigned long>(packet->sequence),
            static_cast<unsigned long>(downlink_debug_statistics_.duplicate_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.late_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.trimmed_packets));
        return;
    }
    it->second = std::move(packet);
    downlink_last_sample_rate_ = it->second->sample_rate;
    downlink_last_frame_duration_ = std::max(1, it->second->frame_duration);
    ObserveDownlinkPacketLocked(*it->second);
    if (downlink_jitter_buffer_.size() > downlink_debug_statistics_.max_jitter_buffer_packets) {
        downlink_debug_statistics_.max_jitter_buffer_packets = static_cast<uint32_t>(downlink_jitter_buffer_.size());
    }

    MaybeStartDownlinkPlaybackLocked();
    TrimJitterBufferLocked();
}

void AudioService::MaybeStartDownlinkPlaybackLocked() {
    if (downlink_playback_started_ || downlink_jitter_buffer_.empty()) {
        return;
    }

    auto start = downlink_jitter_buffer_.begin();
    const auto start_sequence = start->first;
    const auto target_packets = GetTargetJitterBufferPackets(start->second->frame_duration);
    if (downlink_jitter_buffer_.size() >= target_packets) {
        downlink_playback_started_ = true;
        expected_downlink_sequence_ = start_sequence;
        downlink_consecutive_loss_count_ = 0;
        ClearDownlinkRecoveryDeadlineLocked();
        ESP_LOGI(TAG,
            "Starting downlink playback at seq=%lu buffered_packets=%u target_packets=%u target_ms=%lu",
            static_cast<unsigned long>(start_sequence),
            static_cast<unsigned>(downlink_jitter_buffer_.size()),
            static_cast<unsigned>(target_packets),
            static_cast<unsigned long>(downlink_current_target_ms_));
    }
}

void AudioService::TrimJitterBufferLocked() {
    if (downlink_jitter_buffer_.empty() || !downlink_playback_started_) {
        return;
    }

    const auto target_packets = GetTargetJitterBufferPackets(downlink_jitter_buffer_.begin()->second->frame_duration);
    const auto max_packets = GetMaxJitterBufferPackets(downlink_jitter_buffer_.begin()->second->frame_duration);
    while (downlink_jitter_buffer_.size() > max_packets) {
        auto last = std::prev(downlink_jitter_buffer_.end());
        downlink_debug_statistics_.trimmed_packets++;
        const bool should_warn_trim = downlink_debug_statistics_.trimmed_packets <= 4 ||
            (downlink_debug_statistics_.trimmed_packets <= 200 &&
                downlink_debug_statistics_.trimmed_packets % 50 == 0) ||
            downlink_debug_statistics_.trimmed_packets % 200 == 0;
        const char* message = downlink_debug_statistics_.trimmed_packets <= 4 ?
            "Jitter buffer cap reached" :
            "Jitter buffer trim continuing";
        if (should_warn_trim) {
            ESP_LOGW(TAG,
                "%s: seq=%lu buffered_packets=%u expected_seq=%lu target_packets=%u max_packets=%u trimmed=%lu normal=%lu fec=%lu plc=%lu starvation_plc=%lu late=%lu duplicate=%lu",
                message,
                static_cast<unsigned long>(last->first),
                static_cast<unsigned>(downlink_jitter_buffer_.size()),
                static_cast<unsigned long>(expected_downlink_sequence_),
                static_cast<unsigned>(target_packets),
                static_cast<unsigned>(max_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.trimmed_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.normal_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.fec_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.plc_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.starvation_plc_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.late_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.duplicate_packets));
        } else {
            ESP_LOGD(TAG,
                "%s: seq=%lu buffered_packets=%u expected_seq=%lu target_packets=%u max_packets=%u trimmed=%lu normal=%lu fec=%lu plc=%lu starvation_plc=%lu late=%lu duplicate=%lu",
                message,
                static_cast<unsigned long>(last->first),
                static_cast<unsigned>(downlink_jitter_buffer_.size()),
                static_cast<unsigned long>(expected_downlink_sequence_),
                static_cast<unsigned>(target_packets),
                static_cast<unsigned>(max_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.trimmed_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.normal_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.fec_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.plc_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.starvation_plc_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.late_packets),
                static_cast<unsigned long>(downlink_debug_statistics_.duplicate_packets));
        }
        downlink_jitter_buffer_.erase(last);
    }
    MaybeResyncDownlinkAfterHardFailureLocked(max_packets);
}

bool AudioService::CanProduceJitterPlaybackLocked() {
    MaybeStartDownlinkPlaybackLocked();
    if (!downlink_playback_started_) {
        return false;
    }

    if (!downlink_jitter_buffer_.empty()) {
        const auto max_packets = GetMaxJitterBufferPackets(downlink_jitter_buffer_.begin()->second->frame_duration);
        MaybeResyncDownlinkAfterHardFailureLocked(max_packets);
    }

    if (downlink_jitter_buffer_.find(expected_downlink_sequence_) != downlink_jitter_buffer_.end()) {
        ClearDownlinkRecoveryDeadlineLocked();
        return true;
    }

    auto next = downlink_jitter_buffer_.find(expected_downlink_sequence_ + 1);
    if (next != downlink_jitter_buffer_.end()) {
        if (ShouldWaitForDownlinkRecoveryLocked(next->second->frame_duration)) {
            return false;
        }
        return true;
    }

    if (!downlink_jitter_buffer_.empty() && downlink_jitter_buffer_.begin()->first > expected_downlink_sequence_ + 1) {
        if (ShouldWaitForDownlinkRecoveryLocked(downlink_jitter_buffer_.begin()->second->frame_duration)) {
            return false;
        }
        return true;
    }

    return false;
}

bool AudioService::PrepareDownlinkDecodeActionLocked(DownlinkDecodeAction& action) {
    if (!CanProduceJitterPlaybackLocked()) {
        return false;
    }

    if (auto current = downlink_jitter_buffer_.find(expected_downlink_sequence_); current != downlink_jitter_buffer_.end()) {
        action.mode = DownlinkDecodeMode::kNormal;
        action.sample_rate = current->second->sample_rate;
        action.frame_duration = current->second->frame_duration;
        action.timestamp = current->second->timestamp;
        action.sequence = current->first;
        action.generation = downlink_generation_;
        action.recovery_frame = downlink_recovery_boundary_pending_;
        action.loss_count_before_recovery = action.recovery_frame ? downlink_consecutive_loss_count_ : 0;
        action.payload = std::move(current->second->payload);
        downlink_jitter_buffer_.erase(current);
        expected_downlink_sequence_++;
        downlink_consecutive_loss_count_ = 0;
        downlink_consecutive_plc_count_ = 0;
        downlink_recovery_boundary_pending_ = false;
        ClearDownlinkRecoveryDeadlineLocked();
        downlink_debug_statistics_.normal_packets++;
        return true;
    }

    if (auto next = downlink_jitter_buffer_.find(expected_downlink_sequence_ + 1); next != downlink_jitter_buffer_.end()) {
        action.mode = DownlinkDecodeMode::kFec;
        action.sample_rate = next->second->sample_rate;
        action.frame_duration = next->second->frame_duration;
        action.timestamp = 0;
        action.sequence = expected_downlink_sequence_;
        action.generation = downlink_generation_;
        action.recovery_frame = true;
        action.loss_count_before_recovery = downlink_consecutive_loss_count_ + 1;
        action.payload = next->second->payload;
        expected_downlink_sequence_++;
        downlink_consecutive_loss_count_ = action.loss_count_before_recovery;
        downlink_consecutive_plc_count_ = 0;
        downlink_recovery_boundary_pending_ = true;
        ClearDownlinkRecoveryDeadlineLocked();
        downlink_debug_statistics_.fec_packets++;
        return true;
    }

    auto next_available = downlink_jitter_buffer_.begin();
    if (next_available != downlink_jitter_buffer_.end() && next_available->first > expected_downlink_sequence_ + 1) {
        action.mode = DownlinkDecodeMode::kPlc;
        action.sample_rate = next_available->second->sample_rate;
        action.frame_duration = next_available->second->frame_duration;
        action.timestamp = 0;
        action.sequence = expected_downlink_sequence_;
        action.generation = downlink_generation_;
        action.recovery_frame = true;
        action.loss_count_before_recovery = downlink_consecutive_loss_count_ + 1;
        expected_downlink_sequence_++;
        downlink_consecutive_loss_count_ = action.loss_count_before_recovery;
        downlink_consecutive_plc_count_++;
        downlink_recovery_boundary_pending_ = true;
        ClearDownlinkRecoveryDeadlineLocked();
        downlink_debug_statistics_.plc_packets++;
        return true;
    }

    return false;
}

bool AudioService::ExecuteDownlinkDecodeAction(const DownlinkDecodeAction& action, std::unique_ptr<AudioTask>& task) {
    task = std::make_unique<AudioTask>();
    task->type = kAudioTaskTypeDecodeToPlaybackQueue;
    task->timestamp = action.timestamp;
    task->sequence = action.sequence;
    task->frame_duration = action.frame_duration;
    task->is_udp_downlink = action.sequence != 0;
    task->downlink_generation = action.generation;
    task->downlink_decode_mode = static_cast<uint8_t>(action.mode);
    task->downlink_loss_count_before_recovery = action.loss_count_before_recovery;

    SetDecodeSampleRate(action.sample_rate, action.frame_duration);

    bool decoded = false;
    switch (action.mode) {
        case DownlinkDecodeMode::kNormal:
            decoded = downlink_decoder_->Decode(action.payload, task->pcm);
            break;
        case DownlinkDecodeMode::kFec:
            decoded = downlink_decoder_->DecodeFec(action.payload, task->pcm);
            break;
        case DownlinkDecodeMode::kPlc:
            decoded = downlink_decoder_->DecodePacketLoss(task->pcm);
            break;
    }

    if (!decoded && action.mode != DownlinkDecodeMode::kPlc) {
        task->timestamp = 0;
        decoded = downlink_decoder_->DecodePacketLoss(task->pcm);
    }

    if (!decoded) {
        ESP_LOGW(TAG, "Failed to decode downlink audio, mode=%d", static_cast<int>(action.mode));
        return false;
    }

    if (downlink_decoder_->sample_rate() != codec_->output_sample_rate()) {
        const int target_size = output_resampler_.GetOutputSamples(task->pcm.size());
        std::vector<int16_t> resampled(target_size);
        output_resampler_.Process(task->pcm.data(), task->pcm.size(), resampled.data());
        task->pcm = std::move(resampled);
    }

    ApplyDownlinkRecoveryDeclick(action, task->pcm);
    return true;
}

void AudioService::ObserveDownlinkPacketLocked(const AudioStreamPacket& packet) {
    const auto now = std::chrono::steady_clock::now();
    uint32_t arrival_interval_ms = 0;
    if (downlink_last_arrival_time_valid_) {
        arrival_interval_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - downlink_last_arrival_time_).count());
        if (arrival_interval_ms > downlink_debug_statistics_.max_arrival_interval_ms) {
            downlink_debug_statistics_.max_arrival_interval_ms = arrival_interval_ms;
        }
    }
    downlink_last_arrival_time_ = now;
    downlink_last_arrival_time_valid_ = true;

    if (!downlink_last_arrival_sequence_valid_ || packet.sequence > downlink_last_arrival_sequence_) {
        if (downlink_last_arrival_sequence_valid_ && packet.sequence > downlink_last_arrival_sequence_ + 1) {
            const uint32_t gap = packet.sequence - downlink_last_arrival_sequence_ - 1;
            downlink_debug_statistics_.sequence_gap_events++;
            if (gap > downlink_debug_statistics_.max_sequence_gap) {
                downlink_debug_statistics_.max_sequence_gap = gap;
            }
            const bool should_log_gap = downlink_debug_statistics_.sequence_gap_events <= 4 ||
                downlink_debug_statistics_.sequence_gap_events % 50 == 0;
            if (should_log_gap) {
                ESP_LOGD(TAG,
                    "Downlink arrival sequence gap: previous_high=%lu current=%lu gap=%lu interval_ms=%lu jitter_packets=%u expected=%lu gaps=%lu",
                    static_cast<unsigned long>(downlink_last_arrival_sequence_),
                    static_cast<unsigned long>(packet.sequence),
                    static_cast<unsigned long>(gap),
                    static_cast<unsigned long>(arrival_interval_ms),
                    static_cast<unsigned>(downlink_jitter_buffer_.size()),
                    static_cast<unsigned long>(expected_downlink_sequence_),
                    static_cast<unsigned long>(downlink_debug_statistics_.sequence_gap_events));
            }
        }
        downlink_last_arrival_sequence_ = packet.sequence;
        downlink_last_arrival_sequence_valid_ = true;
    }
}

void AudioService::ObserveDecodedDownlinkPcmLocked(const AudioTask& task) {
    if (!task.is_udp_downlink || task.pcm.empty()) {
        return;
    }

    int32_t peak = 0;
    for (const auto sample : task.pcm) {
        const int32_t sample_abs = AbsInt32(static_cast<int32_t>(sample));
        if (sample_abs > peak) {
            peak = sample_abs;
        }
    }
    if (peak > downlink_debug_statistics_.max_pcm_peak) {
        downlink_debug_statistics_.max_pcm_peak = peak;
    }
    if (peak >= kDownlinkClippingPeakWarning) {
        downlink_debug_statistics_.clipping_frames++;
        ESP_LOGW(TAG,
            "Downlink PCM near clipping: seq=%lu mode=%u peak=%ld samples=%u clipping_frames=%lu",
            static_cast<unsigned long>(task.sequence),
            static_cast<unsigned>(task.downlink_decode_mode),
            static_cast<long>(peak),
            static_cast<unsigned>(task.pcm.size()),
            static_cast<unsigned long>(downlink_debug_statistics_.clipping_frames));
    }

    if (downlink_quality_tail_valid_) {
        const int32_t delta = static_cast<int32_t>(task.pcm.front()) - static_cast<int32_t>(downlink_quality_tail_sample_);
        const int32_t abs_delta = AbsInt32(delta);
        if (abs_delta > AbsInt32(downlink_debug_statistics_.max_boundary_delta)) {
            downlink_debug_statistics_.max_boundary_delta = delta;
        }
        if (abs_delta >= kDownlinkBoundaryJumpWarning) {
            downlink_debug_statistics_.boundary_jump_events++;
            ESP_LOGW(TAG,
                "Downlink PCM boundary jump: seq=%lu mode=%u generation=%lu delta=%ld previous=%d first=%d peak=%ld jumps=%lu loss_before=%lu",
                static_cast<unsigned long>(task.sequence),
                static_cast<unsigned>(task.downlink_decode_mode),
                static_cast<unsigned long>(task.downlink_generation),
                static_cast<long>(delta),
                static_cast<int>(downlink_quality_tail_sample_),
                static_cast<int>(task.pcm.front()),
                static_cast<long>(peak),
                static_cast<unsigned long>(downlink_debug_statistics_.boundary_jump_events),
                static_cast<unsigned long>(task.downlink_loss_count_before_recovery));
        }
    }

    downlink_quality_tail_sample_ = task.pcm.back();
    downlink_quality_tail_valid_ = true;
}

void AudioService::ObserveDownlinkOutputLocked(
    const AudioTask& task,
    std::chrono::steady_clock::time_point output_start,
    uint32_t write_ms) {
    if (!task.is_udp_downlink) {
        return;
    }

    if (write_ms > downlink_debug_statistics_.max_output_write_ms) {
        downlink_debug_statistics_.max_output_write_ms = write_ms;
    }
    const uint32_t slow_threshold_ms = std::max<uint32_t>(
        kDownlinkMinSlowOutputWarningMs,
        static_cast<uint32_t>(std::max(1, task.frame_duration) * 2));
    if (write_ms >= slow_threshold_ms) {
        downlink_debug_statistics_.slow_output_events++;
        ESP_LOGW(TAG,
            "Slow downlink OutputData: seq=%lu generation=%lu write_ms=%lu frame_ms=%d playback_queue=%u slow_events=%lu",
            static_cast<unsigned long>(task.sequence),
            static_cast<unsigned long>(task.downlink_generation),
            static_cast<unsigned long>(write_ms),
            task.frame_duration,
            static_cast<unsigned>(audio_playback_queue_.size()),
            static_cast<unsigned long>(downlink_debug_statistics_.slow_output_events));
    }

    if (downlink_last_output_finish_valid_) {
        const uint32_t gap_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(output_start - downlink_last_output_finish_).count());
        if (gap_ms > downlink_debug_statistics_.max_output_gap_ms) {
            downlink_debug_statistics_.max_output_gap_ms = gap_ms;
        }
        if (gap_ms >= kDownlinkOutputGapWarningMs) {
            downlink_debug_statistics_.output_gap_events++;
            ESP_LOGW(TAG,
                "Downlink output gap: seq=%lu generation=%lu gap_ms=%lu frame_ms=%d playback_queue=%u gap_events=%lu",
                static_cast<unsigned long>(task.sequence),
                static_cast<unsigned long>(task.downlink_generation),
                static_cast<unsigned long>(gap_ms),
                task.frame_duration,
                static_cast<unsigned>(audio_playback_queue_.size()),
                static_cast<unsigned long>(downlink_debug_statistics_.output_gap_events));
        }
    }

    if (task.downlink_generation != downlink_generation_) {
        downlink_debug_statistics_.generation_mismatch_events++;
        ESP_LOGW(TAG,
            "Outputting downlink frame after reset generation changed: seq=%lu task_gen=%lu current_gen=%lu mismatches=%lu",
            static_cast<unsigned long>(task.sequence),
            static_cast<unsigned long>(task.downlink_generation),
            static_cast<unsigned long>(downlink_generation_),
            static_cast<unsigned long>(downlink_debug_statistics_.generation_mismatch_events));
    }

    downlink_last_output_finish_ = std::chrono::steady_clock::now();
    downlink_last_output_finish_valid_ = true;
}

void AudioService::ApplyDownlinkRecoveryDeclick(const DownlinkDecodeAction& action, std::vector<int16_t>& pcm) {
    if (pcm.empty()) {
        return;
    }

    const bool is_udp_downlink = action.sequence != 0;
    if (is_udp_downlink && action.recovery_frame && downlink_recovery_tail_valid_) {
        const int previous_sample = downlink_recovery_tail_sample_;
        const int first_sample = pcm.front();
        const int delta = first_sample - previous_sample;
        const size_t declick_samples = std::min(
            pcm.size(),
            static_cast<size_t>(std::max(1, codec_->output_sample_rate() * kDownlinkDeclickDurationMs / 1000)));
        for (size_t i = 0; i < declick_samples; ++i) {
            const int target = pcm[i];
            const int weight = static_cast<int>(i + 1);
            const int smoothed = previous_sample + ((target - previous_sample) * weight) / static_cast<int>(declick_samples);
            pcm[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, smoothed)));
        }

        ESP_LOGD(TAG,
            "Applied downlink recovery de-click: mode=%d seq=%lu loss=%lu delta=%d samples=%u",
            static_cast<int>(action.mode),
            static_cast<unsigned long>(action.sequence),
            static_cast<unsigned long>(action.loss_count_before_recovery),
            delta,
            static_cast<unsigned>(declick_samples));
    }

    if (is_udp_downlink) {
        downlink_recovery_tail_sample_ = pcm.back();
        downlink_recovery_tail_valid_ = true;
    }
}

bool AudioService::DecodePassthroughPacket(std::unique_ptr<AudioStreamPacket> packet, std::unique_ptr<AudioTask>& task) {
    if (packet == nullptr) {
        return false;
    }

    downlink_recovery_boundary_pending_ = false;
    downlink_recovery_tail_valid_ = false;
    downlink_recovery_tail_sample_ = 0;

    DownlinkDecodeAction action;
    action.mode = DownlinkDecodeMode::kNormal;
    action.sample_rate = packet->sample_rate;
    action.frame_duration = packet->frame_duration;
    action.timestamp = packet->timestamp;
    action.payload = std::move(packet->payload);
    return ExecuteDownlinkDecodeAction(action, task);
}

bool AudioService::HasPendingDownlinkPlaybackLocked() const {
    return !audio_decode_queue_.empty() ||
        !passthrough_decode_queue_.empty() ||
        !audio_playback_queue_.empty() ||
        downlink_output_active_ ||
        !downlink_jitter_buffer_.empty();
}

bool AudioService::ShouldWaitForDownlinkRecoveryLocked(int frame_duration_ms) {
    if (!downlink_playback_started_ ||
        audio_playback_queue_.size() < DOWNLINK_RECOVERY_MIN_PLAYBACK_QUEUE_PACKETS) {
        ClearDownlinkRecoveryDeadlineLocked();
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (downlink_recovery_deadline_active_ &&
        downlink_recovery_deadline_sequence_ == expected_downlink_sequence_) {
        return now < downlink_recovery_deadline_;
    }

    const uint32_t wait_ms = static_cast<uint32_t>(std::max(1, frame_duration_ms));
    downlink_recovery_deadline_active_ = true;
    downlink_recovery_deadline_sequence_ = expected_downlink_sequence_;
    downlink_recovery_deadline_ = now + std::chrono::milliseconds(wait_ms);
    downlink_debug_statistics_.recovery_waits++;
    if (wait_ms > downlink_debug_statistics_.max_recovery_wait_ms) {
        downlink_debug_statistics_.max_recovery_wait_ms = wait_ms;
    }
    return true;
}

void AudioService::ClearDownlinkRecoveryDeadlineLocked() {
    downlink_recovery_deadline_active_ = false;
    downlink_recovery_deadline_sequence_ = 0;
}

bool AudioService::MaybeResyncDownlinkAfterHardFailureLocked(size_t max_packets) {
    if (!downlink_playback_started_ ||
        downlink_jitter_buffer_.empty()) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (downlink_last_resync_time_valid_ &&
        now - downlink_last_resync_time_ < std::chrono::milliseconds(DOWNLINK_RESYNC_COOLDOWN_MS)) {
        return false;
    }

    const auto buffer_first = downlink_jitter_buffer_.begin()->first;
    const auto buffer_last = downlink_jitter_buffer_.rbegin()->first;
    const uint32_t new_output_gaps =
        downlink_debug_statistics_.output_gap_events - downlink_resync_baseline_output_gaps_;
    const uint32_t new_trimmed_packets =
        downlink_debug_statistics_.trimmed_packets - downlink_resync_baseline_trimmed_packets_;
    const bool has_new_output_gap = new_output_gaps > 0;
    const bool has_trim_storm = new_trimmed_packets >= DOWNLINK_RESYNC_TRIM_THRESHOLD;
    const bool has_consecutive_plc = downlink_consecutive_plc_count_ > DOWNLINK_RESYNC_PLC_THRESHOLD;
    const bool buffer_too_far_ahead = buffer_first > expected_downlink_sequence_ + max_packets;
    const bool hard_failure = has_new_output_gap ||
        has_trim_storm ||
        has_consecutive_plc ||
        buffer_too_far_ahead;
    if (!hard_failure) {
        return false;
    }

    const auto target_packets = GetTargetJitterBufferPackets(downlink_jitter_buffer_.begin()->second->frame_duration);
    uint32_t new_expected = buffer_first;
    const char* reason = "buffer_ahead";
    if (has_new_output_gap || has_trim_storm) {
        const size_t keep_packets = std::min(target_packets, downlink_jitter_buffer_.size());
        auto anchor = downlink_jitter_buffer_.end();
        for (size_t i = 0; i < keep_packets; ++i) {
            --anchor;
        }
        new_expected = anchor->first;
        reason = has_new_output_gap ? "output_gap" : "trim_storm";
    } else if (has_consecutive_plc) {
        reason = "consecutive_plc";
    }

    if (new_expected <= expected_downlink_sequence_) {
        return false;
    }

    const uint32_t old_expected = expected_downlink_sequence_;
    const uint32_t consecutive_plc_before_resync = downlink_consecutive_plc_count_;
    uint32_t dropped_jitter = 0;
    while (!downlink_jitter_buffer_.empty() && downlink_jitter_buffer_.begin()->first < new_expected) {
        downlink_jitter_buffer_.erase(downlink_jitter_buffer_.begin());
        dropped_jitter++;
    }

    uint32_t dropped_playback = 0;
    std::deque<std::unique_ptr<AudioTask>> kept_playback;
    while (!audio_playback_queue_.empty()) {
        auto task = std::move(audio_playback_queue_.front());
        audio_playback_queue_.pop_front();
        if (task != nullptr && task->is_udp_downlink) {
            dropped_playback++;
        } else {
            kept_playback.push_back(std::move(task));
        }
    }
    audio_playback_queue_ = std::move(kept_playback);

    expected_downlink_sequence_ = new_expected;
    downlink_consecutive_loss_count_ = 0;
    downlink_consecutive_plc_count_ = 0;
    downlink_recovery_boundary_pending_ = false;
    downlink_recovery_tail_valid_ = false;
    downlink_recovery_tail_sample_ = 0;
    ClearDownlinkRecoveryDeadlineLocked();

    downlink_debug_statistics_.resync_events++;
    downlink_debug_statistics_.resync_dropped_playback_packets += dropped_playback;
    downlink_debug_statistics_.resync_dropped_jitter_packets += dropped_jitter;
    const uint32_t skipped = new_expected - old_expected;
    downlink_debug_statistics_.resync_skip_packets += skipped;
    downlink_last_resync_time_ = now;
    downlink_last_resync_time_valid_ = true;
    downlink_resync_baseline_output_gaps_ = downlink_debug_statistics_.output_gap_events;
    downlink_resync_baseline_trimmed_packets_ = downlink_debug_statistics_.trimmed_packets;
    downlink_resync_baseline_plc_packets_ = downlink_debug_statistics_.plc_packets;

    ESP_LOGW(TAG,
        "Downlink resync after hard failure: reason=%s old_expected=%lu new_expected=%lu skipped=%lu dropped_playback=%lu dropped_jitter=%lu resync_events=%lu buffer_first=%lu buffer_last=%lu output_gaps=%lu trimmed=%lu plc=%lu consecutive_plc=%lu",
        reason,
        static_cast<unsigned long>(old_expected),
        static_cast<unsigned long>(expected_downlink_sequence_),
        static_cast<unsigned long>(skipped),
        static_cast<unsigned long>(dropped_playback),
        static_cast<unsigned long>(dropped_jitter),
        static_cast<unsigned long>(downlink_debug_statistics_.resync_events),
        static_cast<unsigned long>(buffer_first),
        static_cast<unsigned long>(buffer_last),
        static_cast<unsigned long>(downlink_debug_statistics_.output_gap_events),
        static_cast<unsigned long>(downlink_debug_statistics_.trimmed_packets),
        static_cast<unsigned long>(downlink_debug_statistics_.plc_packets),
        static_cast<unsigned long>(consecutive_plc_before_resync));
    return true;
}

uint32_t AudioService::AdaptDownlinkTargetAfterSessionLocked(const DownlinkDebugStatistics& stats) const {
    const bool hard_failure = stats.resync_events > 0 ||
        stats.output_gap_events > 0 ||
        stats.trimmed_packets > 0 ||
        stats.plc_packets > DOWNLINK_RESYNC_PLC_THRESHOLD ||
        stats.max_arrival_interval_ms > DOWNLINK_HARD_ARRIVAL_MS;
    if (hard_failure) {
        return DOWNLINK_STRONG_TARGET_JITTER_MS;
    }

    const bool should_promote = stats.plc_packets > 5 ||
        stats.late_packets > 20 ||
        stats.output_gap_events > 0 ||
        stats.max_arrival_interval_ms > downlink_current_target_ms_ + 30;
    if (should_promote) {
        return PromoteDownlinkTargetMs(downlink_current_target_ms_);
    }

    const bool clean = stats.plc_packets == 0 &&
        stats.late_packets <= 2 &&
        stats.output_gap_events == 0 &&
        stats.max_arrival_interval_ms <= DOWNLINK_BASE_TARGET_JITTER_MS;
    if (clean) {
        return DemoteDownlinkTargetMs(downlink_current_target_ms_);
    }

    return downlink_current_target_ms_;
}

void AudioService::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    sample_rate = std::max(8000, sample_rate);
    frame_duration = std::max(1, frame_duration);

    if (downlink_decoder_ != nullptr &&
        downlink_decoder_->sample_rate() == sample_rate &&
        downlink_decoder_->duration_ms() == frame_duration) {
        return;
    }

    downlink_decoder_.reset();
    downlink_decoder_ = std::make_unique<OpusStreamDecoder>(sample_rate, 1, frame_duration);
    downlink_recovery_boundary_pending_ = false;
    downlink_recovery_tail_valid_ = false;
    downlink_recovery_tail_sample_ = 0;

    auto codec = Board::GetInstance().GetAudioCodec();
    if (downlink_decoder_->sample_rate() != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", downlink_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(downlink_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void AudioService::PushTaskToEncodeQueue(AudioTaskType type, std::vector<int16_t>&& pcm) {
    auto task = std::make_unique<AudioTask>();
    task->type = type;
    task->pcm = std::move(pcm);
    
    /* Push the task to the encode queue */
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);

    /* If the task is to send queue, we need to set the timestamp */
    if (type == kAudioTaskTypeEncodeToSendQueue && !timestamp_queue_.empty()) {
        if (timestamp_queue_.size() <= MAX_TIMESTAMPS_IN_QUEUE) {
            task->timestamp = timestamp_queue_.front();
        } else {
            ESP_LOGW(TAG, "Timestamp queue (%u) is full, dropping timestamp", timestamp_queue_.size());
        }
        timestamp_queue_.pop_front();
    }

    audio_queue_cv_.wait(lock, [this]() { return audio_encode_queue_.size() < MAX_ENCODE_TASKS_IN_QUEUE; });
    audio_encode_queue_.push_back(std::move(task));
    audio_queue_cv_.notify_all();
}

bool AudioService::IsAudioPlaybackQueueEmpty() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return !HasPendingDownlinkPlaybackLocked();
}

bool AudioService::PushPacketToDecodeQueue(std::unique_ptr<AudioStreamPacket> packet, bool wait) {
    if (packet == nullptr) {
        return false;
    }

    const auto packet_duration_ms = GetPacketDurationMs(*packet);
    std::unique_lock<std::mutex> lock(audio_queue_mutex_);
    if (queued_decode_audio_ms_ + packet_duration_ms > MAX_DECODE_QUEUE_AUDIO_MS) {
        if (wait) {
            audio_queue_cv_.wait(lock, [this, packet_duration_ms]() {
                return service_stopped_ || queued_decode_audio_ms_ + packet_duration_ms <= MAX_DECODE_QUEUE_AUDIO_MS;
            });
            if (service_stopped_) {
                return false;
            }
        } else {
            ESP_LOGW(TAG, "Dropping downlink packet: decode queue full, queued_audio_ms=%u incoming_duration_ms=%u",
                static_cast<unsigned>(queued_decode_audio_ms_), static_cast<unsigned>(packet_duration_ms));
            return false;
        }
    }
    queued_decode_audio_ms_ += packet_duration_ms;
    if (queued_decode_audio_ms_ > downlink_debug_statistics_.max_decode_queue_audio_ms) {
        downlink_debug_statistics_.max_decode_queue_audio_ms = static_cast<uint32_t>(queued_decode_audio_ms_);
    }
    audio_decode_queue_.push_back(std::move(packet));
    audio_queue_cv_.notify_all();
    return true;
}

std::unique_ptr<AudioStreamPacket> AudioService::PopPacketFromSendQueue() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    if (audio_send_queue_.empty()) {
        return nullptr;
    }
    auto packet = std::move(audio_send_queue_.front());
    audio_send_queue_.pop_front();
    audio_queue_cv_.notify_all();
    return packet;
}

void AudioService::EncodeWakeWord() {
    if (wake_word_) {
        wake_word_->EncodeWakeWordData();
    }
}

const std::string& AudioService::GetLastWakeWord() const {
    return wake_word_->GetLastDetectedWakeWord();
}

std::unique_ptr<AudioStreamPacket> AudioService::PopWakeWordPacket() {
    auto packet = std::make_unique<AudioStreamPacket>();
    if (wake_word_->GetWakeWordOpus(packet->payload)) {
        return packet;
    }
    return nullptr;
}

void AudioService::EnableWakeWordDetection(bool enable) {
    if (!wake_word_) {
        return;
    }

    ESP_LOGD(TAG, "%s wake word detection", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!wake_word_initialized_) {
            if (!wake_word_->Initialize(codec_, models_list_)) {
                ESP_LOGE(TAG, "Failed to initialize wake word");
                return;
            }
            wake_word_initialized_ = true;
        }
        wake_word_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    } else {
        wake_word_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_WAKE_WORD_RUNNING);
    }
}

void AudioService::EnableVoiceProcessing(bool enable) {
    ESP_LOGD(TAG, "%s voice processing", enable ? "Enabling" : "Disabling");
    if (enable) {
        if (!audio_processor_initialized_) {
            audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
            audio_processor_initialized_ = true;
        }

        /* We should make sure no audio is playing */
        ResetDecoder();
        audio_input_need_warmup_ = true;
        audio_processor_->Start();
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    } else {
        audio_processor_->Stop();
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_PROCESSOR_RUNNING);
    }
}

void AudioService::EnableAudioTesting(bool enable) {
    ESP_LOGI(TAG, "%s audio testing", enable ? "Enabling" : "Disabling");
    if (enable) {
        xEventGroupSetBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
    } else {
        xEventGroupClearBits(event_group_, AS_EVENT_AUDIO_TESTING_RUNNING);
        std::lock_guard<std::mutex> lock(audio_queue_mutex_);
        while (!audio_testing_queue_.empty()) {
            auto packet = std::move(audio_testing_queue_.front());
            audio_testing_queue_.pop_front();
            queued_decode_audio_ms_ += GetPacketDurationMs(*packet);
            audio_decode_queue_.push_back(std::move(packet));
        }
        audio_queue_cv_.notify_all();
    }
}

void AudioService::EnableDeviceAec(bool enable) {
    ESP_LOGI(TAG, "%s device AEC", enable ? "Enabling" : "Disabling");
    if (!audio_processor_initialized_) {
        audio_processor_->Initialize(codec_, OPUS_FRAME_DURATION_MS, models_list_);
        audio_processor_initialized_ = true;
    }

    audio_processor_->EnableDeviceAec(enable);
}

void AudioService::SetCallbacks(AudioServiceCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void AudioService::PlaySound(const std::string_view& ogg) {
    if (!codec_->output_enabled()) {
        esp_timer_stop(audio_power_timer_);
        esp_timer_start_periodic(audio_power_timer_, AUDIO_POWER_CHECK_INTERVAL_MS * 1000);
        codec_->EnableOutput(true);
    }

    const uint8_t* buf = reinterpret_cast<const uint8_t*>(ogg.data());
    size_t size = ogg.size();
    size_t offset = 0;

    auto find_page = [&](size_t start)->size_t {
        for (size_t i = start; i + 4 <= size; ++i) {
            if (buf[i] == 'O' && buf[i+1] == 'g' && buf[i+2] == 'g' && buf[i+3] == 'S') return i;
        }
        return static_cast<size_t>(-1);
    };

    bool seen_head = false;
    bool seen_tags = false;
    int sample_rate = 16000; // 默认值

    while (true) {
        size_t pos = find_page(offset);
        if (pos == static_cast<size_t>(-1)) break;
        offset = pos;
        if (offset + 27 > size) break;

        const uint8_t* page = buf + offset;
        uint8_t page_segments = page[26];
        size_t seg_table_off = offset + 27;
        if (seg_table_off + page_segments > size) break;

        size_t body_size = 0;
        for (size_t i = 0; i < page_segments; ++i) body_size += page[27 + i];

        size_t body_off = seg_table_off + page_segments;
        if (body_off + body_size > size) break;

        // Parse packets using lacing
        size_t cur = body_off;
        size_t seg_idx = 0;
        while (seg_idx < page_segments) {
            size_t pkt_len = 0;
            size_t pkt_start = cur;
            bool continued = false;
            do {
                uint8_t l = page[27 + seg_idx++];
                pkt_len += l;
                cur += l;
                continued = (l == 255);
            } while (continued && seg_idx < page_segments);

            if (pkt_len == 0) continue;
            const uint8_t* pkt_ptr = buf + pkt_start;

            if (!seen_head) {
                // 解析OpusHead包
                if (pkt_len >= 19 && std::memcmp(pkt_ptr, "OpusHead", 8) == 0) {
                    seen_head = true;
                    
                    // OpusHead结构：[0-7] "OpusHead", [8] version, [9] channel_count, [10-11] pre_skip
                    // [12-15] input_sample_rate, [16-17] output_gain, [18] mapping_family
                    if (pkt_len >= 12) {
                        uint8_t version = pkt_ptr[8];
                        uint8_t channel_count = pkt_ptr[9];
                        
                        if (pkt_len >= 16) {
                            // 读取输入采样率 (little-endian)
                            sample_rate = pkt_ptr[12] | (pkt_ptr[13] << 8) | 
                                        (pkt_ptr[14] << 16) | (pkt_ptr[15] << 24);
                            ESP_LOGI(TAG, "OpusHead: version=%d, channels=%d, sample_rate=%d", 
                                   version, channel_count, sample_rate);
                        }
                    }
                }
                continue;
            }
            if (!seen_tags) {
                // Expect OpusTags in second packet
                if (pkt_len >= 8 && std::memcmp(pkt_ptr, "OpusTags", 8) == 0) {
                    seen_tags = true;
                }
                continue;
            }

            // Audio packet (Opus)
            auto packet = std::make_unique<AudioStreamPacket>();
            packet->sample_rate = sample_rate;
            packet->frame_duration = 60;
            packet->payload.resize(pkt_len);
            std::memcpy(packet->payload.data(), pkt_ptr, pkt_len);
            PushPacketToDecodeQueue(std::move(packet), true);
        }

        offset = body_off + body_size;
    }
}

bool AudioService::IsIdle() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    return audio_encode_queue_.empty() &&
        audio_send_queue_.empty() &&
        audio_decode_queue_.empty() &&
        passthrough_decode_queue_.empty() &&
        audio_playback_queue_.empty() &&
        audio_testing_queue_.empty() &&
        downlink_jitter_buffer_.empty();
}

void AudioService::ResetDecoder() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    ResetDecoderStateLocked();
    audio_queue_cv_.notify_all();
}

void AudioService::ResetDecoderStateLocked() {
    const bool has_downlink_stats = downlink_debug_statistics_.normal_packets != 0 ||
        downlink_debug_statistics_.fec_packets != 0 ||
        downlink_debug_statistics_.plc_packets != 0 ||
        downlink_debug_statistics_.starvation_plc_packets != 0 ||
        downlink_debug_statistics_.late_packets != 0 ||
        downlink_debug_statistics_.duplicate_packets != 0 ||
        downlink_debug_statistics_.trimmed_packets != 0 ||
        downlink_debug_statistics_.sequence_gap_events != 0 ||
        downlink_debug_statistics_.boundary_jump_events != 0 ||
        downlink_debug_statistics_.clipping_frames != 0 ||
        downlink_debug_statistics_.slow_output_events != 0 ||
        downlink_debug_statistics_.output_gap_events != 0 ||
        downlink_debug_statistics_.generation_mismatch_events != 0 ||
        downlink_debug_statistics_.recovery_waits != 0 ||
        downlink_debug_statistics_.resync_events != 0;
    const uint32_t next_target_ms = has_downlink_stats ?
        AdaptDownlinkTargetAfterSessionLocked(downlink_debug_statistics_) :
        downlink_next_target_ms_;
    if (has_downlink_stats) {
        ESP_LOGI(TAG,
            "Resetting downlink stats: gen=%lu target_ms=%lu next_target_ms=%lu normal=%lu fec=%lu plc=%lu starvation_plc=%lu late=%lu duplicate=%lu trimmed=%lu seq_gaps=%lu max_seq_gap=%lu resync=%lu skipped=%lu dropped_playback=%lu dropped_jitter=%lu",
            static_cast<unsigned long>(downlink_generation_),
            static_cast<unsigned long>(downlink_current_target_ms_),
            static_cast<unsigned long>(next_target_ms),
            static_cast<unsigned long>(downlink_debug_statistics_.normal_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.fec_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.plc_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.starvation_plc_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.late_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.duplicate_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.trimmed_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.sequence_gap_events),
            static_cast<unsigned long>(downlink_debug_statistics_.max_sequence_gap),
            static_cast<unsigned long>(downlink_debug_statistics_.resync_events),
            static_cast<unsigned long>(downlink_debug_statistics_.resync_skip_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.resync_dropped_playback_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.resync_dropped_jitter_packets));
        ESP_LOGI(TAG,
            "Downlink quality summary: gen=%lu target_ms=%lu next_target_ms=%lu max_arrival_ms=%lu max_jitter=%lu max_decode_ms=%lu max_playback=%lu recovery_waits=%lu max_recovery_wait_ms=%lu jumps=%lu max_delta=%ld clipping=%lu max_peak=%ld slow_output=%lu max_write_ms=%lu output_gaps=%lu max_gap_ms=%lu gen_mismatch=%lu",
            static_cast<unsigned long>(downlink_generation_),
            static_cast<unsigned long>(downlink_current_target_ms_),
            static_cast<unsigned long>(next_target_ms),
            static_cast<unsigned long>(downlink_debug_statistics_.max_arrival_interval_ms),
            static_cast<unsigned long>(downlink_debug_statistics_.max_jitter_buffer_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.max_decode_queue_audio_ms),
            static_cast<unsigned long>(downlink_debug_statistics_.max_playback_queue_packets),
            static_cast<unsigned long>(downlink_debug_statistics_.recovery_waits),
            static_cast<unsigned long>(downlink_debug_statistics_.max_recovery_wait_ms),
            static_cast<unsigned long>(downlink_debug_statistics_.boundary_jump_events),
            static_cast<long>(downlink_debug_statistics_.max_boundary_delta),
            static_cast<unsigned long>(downlink_debug_statistics_.clipping_frames),
            static_cast<long>(downlink_debug_statistics_.max_pcm_peak),
            static_cast<unsigned long>(downlink_debug_statistics_.slow_output_events),
            static_cast<unsigned long>(downlink_debug_statistics_.max_output_write_ms),
            static_cast<unsigned long>(downlink_debug_statistics_.output_gap_events),
            static_cast<unsigned long>(downlink_debug_statistics_.max_output_gap_ms),
            static_cast<unsigned long>(downlink_debug_statistics_.generation_mismatch_events));
    }
    downlink_next_target_ms_ = next_target_ms;

    downlink_generation_++;
    if (downlink_decoder_ != nullptr) {
        downlink_decoder_->ResetState();
    }
    timestamp_queue_.clear();
    audio_decode_queue_.clear();
    passthrough_decode_queue_.clear();
    audio_playback_queue_.clear();
    audio_testing_queue_.clear();
    downlink_jitter_buffer_.clear();
    queued_decode_audio_ms_ = 0;
    expected_downlink_sequence_ = 0;
    downlink_consecutive_loss_count_ = 0;
    downlink_consecutive_plc_count_ = 0;
    downlink_playback_started_ = false;
    downlink_output_active_ = false;
    downlink_last_sample_rate_ = 0;
    downlink_last_frame_duration_ = UDP_DOWNLINK_TARGET_FRAME_DURATION_MS;
    downlink_current_target_ms_ = downlink_next_target_ms_;
    ClearDownlinkRecoveryDeadlineLocked();
    downlink_last_resync_time_valid_ = false;
    downlink_resync_baseline_output_gaps_ = 0;
    downlink_resync_baseline_trimmed_packets_ = 0;
    downlink_resync_baseline_plc_packets_ = 0;
    downlink_debug_statistics_ = {};
    downlink_recovery_boundary_pending_ = false;
    downlink_recovery_tail_valid_ = false;
    downlink_recovery_tail_sample_ = 0;
    downlink_last_arrival_time_valid_ = false;
    downlink_last_arrival_sequence_valid_ = false;
    downlink_last_arrival_sequence_ = 0;
    downlink_quality_tail_valid_ = false;
    downlink_quality_tail_sample_ = 0;
    downlink_last_output_finish_valid_ = false;
}

void AudioService::ResetUplink() {
    std::lock_guard<std::mutex> lock(audio_queue_mutex_);
    ResetUplinkLocked();
    audio_queue_cv_.notify_all();
}

void AudioService::ResetUplinkLocked() {
    audio_encode_queue_.clear();
    timestamp_queue_.clear();
    audio_send_queue_.clear();
}

bool AudioService::SupportsDeviceAec() const {
#if CONFIG_USE_DEVICE_AEC
    if (codec_ == nullptr) {
        return false;
    }
    if (!audio_processor_initialized_) {
        return codec_->input_reference();
    }
    return audio_processor_ != nullptr && audio_processor_->SupportsDeviceAec();
#else
    return false;
#endif
}

void AudioService::CheckAndUpdateAudioPowerState() {
    auto now = std::chrono::steady_clock::now();
    auto input_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_input_time_).count();
    auto output_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_output_time_).count();
    if (input_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->input_enabled()) {
        codec_->EnableInput(false);
    }
    if (output_elapsed > AUDIO_POWER_TIMEOUT_MS && codec_->output_enabled()) {
        codec_->EnableOutput(false);
    }
    if (!codec_->input_enabled() && !codec_->output_enabled()) {
        esp_timer_stop(audio_power_timer_);
    }
}

void AudioService::SetModelsList(srmodel_list_t* models_list) {
    models_list_ = models_list;

#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    if (esp_srmodel_filter(models_list_, ESP_MN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<CustomWakeWord>();
    } else if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<AfeWakeWord>();
    } else {
        wake_word_ = nullptr;
    }
#else
    if (esp_srmodel_filter(models_list_, ESP_WN_PREFIX, NULL) != nullptr) {
        wake_word_ = std::make_unique<EspWakeWord>();
    } else {
        wake_word_ = nullptr;
    }
#endif

    if (wake_word_) {
        wake_word_->OnWakeWordDetected([this](const std::string& wake_word) {
            if (callbacks_.on_wake_word_detected) {
                callbacks_.on_wake_word_detected(wake_word);
            }
        });
    }
}

bool AudioService::IsAfeWakeWord() {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
    return wake_word_ != nullptr && dynamic_cast<AfeWakeWord*>(wake_word_.get()) != nullptr;
#else
    return false;
#endif
}
