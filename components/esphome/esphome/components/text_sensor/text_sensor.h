#pragma once

#include <string>

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace text_sensor {

class TextSensor : public EntityBase, public EntityBase_DeviceClass {
 public:
  std::string state;

  void publish_state(const std::string &state);

  void add_on_state_callback(std::function<void(const std::string &)> &&callback);

  void set_unique_id(const std::string &unique_id) { this->unique_id_ = unique_id; }
  std::string unique_id() const { return this->unique_id_; }

 protected:
  std::string unique_id_;
  CallbackManager<void(const std::string &)> state_callback_;
};

}  // namespace text_sensor
}  // namespace esphome
