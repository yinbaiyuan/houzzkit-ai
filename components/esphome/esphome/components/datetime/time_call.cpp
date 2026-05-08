#include "time_call.h"
#include "time_entity.h"
#include "esphome/core/log.h"

namespace esphome {
namespace datetime {

static const char *const TAG = "datetime.time";

TimeCall &TimeCall::set_time(uint8_t hour, uint8_t minute, uint8_t second) {
  this->hour_ = hour;
  this->minute_ = minute;
  this->second_ = second;
  return *this;
}

void TimeCall::perform() {
  const auto *name = this->parent_->get_name().c_str();
  if (!this->hour_.has_value() || !this->minute_.has_value() || !this->second_.has_value()) {
    ESP_LOGW(TAG, "'%s' - TimeCall performed without a complete time", name);
    return;
  }

  uint8_t hour = this->hour_.value();
  uint8_t minute = this->minute_.value();
  uint8_t second = this->second_.value();
  if (hour > 23 || minute > 59 || second > 59) {
    ESP_LOGW(TAG, "'%s' - Invalid time %02u:%02u:%02u", name, hour, minute, second);
    return;
  }

  ESP_LOGD(TAG, "'%s' - Setting time %02u:%02u:%02u", name, hour, minute, second);
  this->parent_->control(hour, minute, second);
}

}  // namespace datetime
}  // namespace esphome
