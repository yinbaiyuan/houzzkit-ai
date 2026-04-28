#ifndef OPUS_STREAM_DECODER_H
#define OPUS_STREAM_DECODER_H

#include <cstdint>
#include <mutex>
#include <vector>

#include "opus.h"

class OpusStreamDecoder {
public:
    OpusStreamDecoder(int sample_rate, int channels, int duration_ms);
    ~OpusStreamDecoder();

    bool Decode(const std::vector<uint8_t>& opus, std::vector<int16_t>& pcm);
    bool DecodeFec(const std::vector<uint8_t>& opus, std::vector<int16_t>& pcm);
    bool DecodePacketLoss(std::vector<int16_t>& pcm);
    void ResetState();

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }
    int duration_ms() const { return duration_ms_; }

private:
    bool DecodeInternal(const uint8_t* data, int len, int decode_fec, std::vector<int16_t>& pcm);

    std::mutex mutex_;
    OpusDecoder* decoder_ = nullptr;
    int sample_rate_ = 0;
    int channels_ = 0;
    int duration_ms_ = 0;
    int frame_size_per_channel_ = 0;
};

#endif
