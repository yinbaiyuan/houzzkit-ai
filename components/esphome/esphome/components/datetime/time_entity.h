#pragma once

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"
#include "time_call.h"

namespace esphome {
namespace datetime {

class TimeEntity : public EntityBase {
 public:
  uint8_t hour{0};
  uint8_t minute{0};
  uint8_t second{0};

  void publish_state(uint8_t hour, uint8_t minute, uint8_t second);

  TimeCall make_call() { return TimeCall(this); }

  void add_on_state_callback(std::function<void()> &&callback);

 protected:
  friend class TimeCall;

  virtual void control(uint8_t hour, uint8_t minute, uint8_t second) = 0;

  CallbackManager<void()> state_callback_;
};

}  // namespace datetime
}  // namespace esphome
