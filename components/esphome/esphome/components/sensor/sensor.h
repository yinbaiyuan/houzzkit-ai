#pragma once

#include "esphome/core/component.h"
#include "esphome/core/entity_base.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace sensor {

enum SensorStateClass : uint8_t {
  STATE_CLASS_NONE = 0,
  STATE_CLASS_MEASUREMENT = 1,
  STATE_CLASS_TOTAL_INCREASING = 2,
  STATE_CLASS_TOTAL = 3,
};

class Sensor : public EntityBase, public EntityBase_DeviceClass, public EntityBase_UnitOfMeasurement {
 public:
  float state{0.0f};

  void publish_state(float state);

  void add_on_state_callback(std::function<void(float)> &&callback);

  void set_unique_id(const std::string &unique_id) { this->unique_id_ = unique_id; }
  std::string unique_id() const { return this->unique_id_; }

  void set_accuracy_decimals(int8_t accuracy_decimals) { this->accuracy_decimals_ = accuracy_decimals; }
  int8_t get_accuracy_decimals() const { return this->accuracy_decimals_; }

  void set_force_update(bool force_update) { this->force_update_ = force_update; }
  bool get_force_update() const { return this->force_update_; }

  void set_state_class(SensorStateClass state_class) { this->state_class_ = state_class; }
  SensorStateClass get_state_class() const { return this->state_class_; }

 protected:
  std::string unique_id_;
  int8_t accuracy_decimals_{1};
  bool force_update_{false};
  SensorStateClass state_class_{STATE_CLASS_NONE};
  CallbackManager<void(float)> state_callback_;
};

}  // namespace sensor
}  // namespace esphome
