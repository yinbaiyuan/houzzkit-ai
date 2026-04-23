#include "opus_stream_decoder.h"

#include <esp_log.h>

namespace {

constexpr const char* TAG = "OpusStreamDecoder";

}  // namespace

OpusStreamDecoder::OpusStreamDecoder(int sample_rate, int channels, int duration_ms)
    : sample_rate_(sample_rate), channels_(channels), duration_ms_(duration_ms) {
    int error = OPUS_OK;
    decoder_ = opus_decoder_create(sample_rate_, channels_, &error);
    if (decoder_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create decoder, error code: %d", error);
        return;
    }

    frame_size_per_channel_ = sample_rate_ * duration_ms_ / 1000;
}

OpusStreamDecoder::~OpusStreamDecoder() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (decoder_ != nullptr) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
}

bool OpusStreamDecoder::Decode(const std::vector<uint8_t>& opus, std::vector<int16_t>& pcm) {
    return DecodeInternal(opus.data(), static_cast<int>(opus.size()), 0, pcm);
}

bool OpusStreamDecoder::DecodeFec(const std::vector<uint8_t>& opus, std::vector<int16_t>& pcm) {
    return DecodeInternal(opus.data(), static_cast<int>(opus.size()), 1, pcm);
}

bool OpusStreamDecoder::DecodePacketLoss(std::vector<int16_t>& pcm) {
    return DecodeInternal(nullptr, 0, 0, pcm);
}

void OpusStreamDecoder::ResetState() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (decoder_ != nullptr) {
        opus_decoder_ctl(decoder_, OPUS_RESET_STATE);
    }
}

bool OpusStreamDecoder::DecodeInternal(const uint8_t* data, int len, int decode_fec, std::vector<int16_t>& pcm) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (decoder_ == nullptr) {
        ESP_LOGE(TAG, "Decoder is not configured");
        return false;
    }

    pcm.resize(frame_size_per_channel_ * channels_);
    const auto ret = opus_decode(decoder_, data, len, pcm.data(), frame_size_per_channel_, decode_fec);
    if (ret < 0) {
        ESP_LOGW(TAG, "Decode failed, error code: %d, fec=%d, len=%d", ret, decode_fec, len);
        return false;
    }

    pcm.resize(ret * channels_);
    return true;
}
