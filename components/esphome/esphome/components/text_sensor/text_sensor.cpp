#include "text_sensor.h"
#include "esphome/core/log.h"

namespace esphome {
namespace text_sensor {

static const char *const TAG = "text_sensor";

void TextSensor::publish_state(const std::string &state) {
  this->set_has_state(true);
  this->state = state;
  ESP_LOGD(TAG, "'%s': Sending state %s", this->get_name().c_str(), state.c_str());
  this->state_callback_.call(this->state);
}

void TextSensor::add_on_state_callback(std::function<void(const std::string &)> &&callback) {
  this->state_callback_.add(std::move(callback));
}

}  // namespace text_sensor
}  // namespace esphome
