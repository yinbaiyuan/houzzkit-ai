#include "sensor.h"

#include "esphome/core/log.h"

namespace esphome {
namespace sensor {

static const char *const TAG = "sensor";

void Sensor::publish_state(float state) {
  this->set_has_state(true);
  this->state = state;
  ESP_LOGD(TAG, "'%s': Sending state %.1f", this->get_name().c_str(), state);
  this->state_callback_.call(this->state);
}

void Sensor::add_on_state_callback(std::function<void(float)> &&callback) {
  this->state_callback_.add(std::move(callback));
}

}  // namespace sensor
}  // namespace esphome
