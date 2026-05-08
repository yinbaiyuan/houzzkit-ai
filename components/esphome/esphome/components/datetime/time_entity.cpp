#include "time_entity.h"
#include "esphome/core/log.h"

namespace esphome {
namespace datetime {

static const char *const TAG = "datetime.time";

void TimeEntity::publish_state(uint8_t hour, uint8_t minute, uint8_t second) {
  this->set_has_state(true);
  this->hour = hour;
  this->minute = minute;
  this->second = second;
  ESP_LOGD(TAG, "'%s': Sending state %02u:%02u:%02u", this->get_name().c_str(), hour, minute, second);
  this->state_callback_.call();
}

void TimeEntity::add_on_state_callback(std::function<void()> &&callback) {
  this->state_callback_.add(std::move(callback));
}

}  // namespace datetime
}  // namespace esphome
