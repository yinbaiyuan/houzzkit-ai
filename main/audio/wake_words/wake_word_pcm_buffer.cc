#include "wake_word_pcm_buffer.h"

#include <algorithm>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>

#define TAG "WakeWordPcmBuffer"

WakeWordPcmBuffer::WakeWordPcmBuffer(size_t sample_rate, size_t duration_ms) {
    capacity_ = sample_rate * duration_ms / 1000;
    data_ = static_cast<int16_t*>(heap_caps_malloc(capacity_ * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (data_ == nullptr) {
        ESP_LOGW(TAG, "Failed to allocate %u bytes PSRAM PCM buffer, fallback to default heap",
                 static_cast<unsigned>(capacity_ * sizeof(int16_t)));
        data_ = static_cast<int16_t*>(heap_caps_malloc(capacity_ * sizeof(int16_t), MALLOC_CAP_8BIT));
    }
    if (data_ == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate wake word PCM buffer");
        capacity_ = 0;
    }
}

WakeWordPcmBuffer::~WakeWordPcmBuffer() {
    if (data_ != nullptr) {
        heap_caps_free(data_);
    }
}

bool WakeWordPcmBuffer::Store(const int16_t* data, size_t samples) {
    if (data_ == nullptr || data == nullptr || capacity_ == 0 || samples == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (samples >= capacity_) {
        std::memcpy(data_, data + samples - capacity_, capacity_ * sizeof(int16_t));
        write_pos_ = 0;
        size_ = capacity_;
        return true;
    }

    const size_t first = std::min(samples, capacity_ - write_pos_);
    std::memcpy(data_ + write_pos_, data, first * sizeof(int16_t));
    if (samples > first) {
        std::memcpy(data_, data + first, (samples - first) * sizeof(int16_t));
    }

    write_pos_ = (write_pos_ + samples) % capacity_;
    size_ = std::min(capacity_, size_ + samples);
    return true;
}

bool WakeWordPcmBuffer::Snapshot(std::vector<int16_t>& output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    output.clear();
    if (data_ == nullptr || size_ == 0 || capacity_ == 0) {
        return false;
    }

    output.resize(size_);
    const size_t start = (write_pos_ + capacity_ - size_) % capacity_;
    const size_t first = std::min(size_, capacity_ - start);
    std::memcpy(output.data(), data_ + start, first * sizeof(int16_t));
    if (size_ > first) {
        std::memcpy(output.data() + first, data_, (size_ - first) * sizeof(int16_t));
    }
    return true;
}

void WakeWordPcmBuffer::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    write_pos_ = 0;
    size_ = 0;
}

size_t WakeWordPcmBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}
