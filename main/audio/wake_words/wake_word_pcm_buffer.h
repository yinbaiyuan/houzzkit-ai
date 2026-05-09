#ifndef WAKE_WORD_PCM_BUFFER_H
#define WAKE_WORD_PCM_BUFFER_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

class WakeWordPcmBuffer {
public:
    WakeWordPcmBuffer(size_t sample_rate = 16000, size_t duration_ms = 2000);
    ~WakeWordPcmBuffer();

    WakeWordPcmBuffer(const WakeWordPcmBuffer&) = delete;
    WakeWordPcmBuffer& operator=(const WakeWordPcmBuffer&) = delete;

    bool Store(const int16_t* data, size_t samples);
    bool Snapshot(std::vector<int16_t>& output) const;
    void Clear();
    size_t size() const;
    size_t capacity() const { return capacity_; }

private:
    int16_t* data_ = nullptr;
    size_t capacity_ = 0;
    size_t write_pos_ = 0;
    size_t size_ = 0;
    mutable std::mutex mutex_;
};

#endif // WAKE_WORD_PCM_BUFFER_H
