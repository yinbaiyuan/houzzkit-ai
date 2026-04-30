#pragma once

#include "esphome/core/helpers.h"

namespace esphome {
namespace datetime {

class TimeEntity;

class TimeCall {
 public:
  explicit TimeCall(TimeEntity *parent) : parent_(parent) {}
  void perform();

  TimeCall &set_time(uint8_t hour, uint8_t minute, uint8_t second);

 protected:
  TimeEntity *const parent_;
  optional<uint8_t> hour_;
  optional<uint8_t> minute_;
  optional<uint8_t> second_;
};

}  // namespace datetime
}  // namespace esphome
