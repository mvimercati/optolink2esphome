#pragma once
// The entities of the datapoints: raw values to states and back. What is written is published
// at once (no stale state for automations); the read-back that follows every write corrects it.
// While the link is down every entity is unknown in Home Assistant, until it is read again.

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "esphome/core/controller_registry.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif

#include "optolink.h"

// Sends "no state" to Home Assistant (the API reports has_state() == false as unknown).
#ifdef USE_CONTROLLER_REGISTRY
#define OPTOLINK_UNKNOWN(kind) \
  do { \
    this->set_has_state(false); \
    ControllerRegistry::notify_##kind##_update(this); \
  } while (0)
#else
#define OPTOLINK_UNKNOWN(kind) this->set_has_state(false)
#endif

namespace esphome {
namespace optolink {

#ifdef USE_SENSOR
// value / divisor, rounded to accuracy_decimals as vitalk2mqtt did: fewer state changes in HA.
class OptolinkSensor : public sensor::Sensor, public Datapoint {
 public:
  void set_divisor(float divisor) { this->divisor_ = divisor; }
  void publish(const uint8_t *data) override {
    float scale = powf(10, this->get_accuracy_decimals());
    this->publish_state(roundf(this->value(data) / this->divisor_ * scale) / scale);
  }
  void invalidate() override { OPTOLINK_UNKNOWN(sensor); }

 protected:
  float divisor_{1};
};
#endif

#ifdef USE_TEXT_SENSOR
// The text of the value in the options, or the value itself; a date as "H:MM D/M/YYYY".
class OptolinkTextSensor : public text_sensor::TextSensor, public Datapoint {
 public:
  void add_option(int32_t value, const char *text) { this->options_.emplace_back(value, text); }
  void publish(const uint8_t *d) override {
    char buf[24];
    if (this->type_ == DATE) {
      snprintf(buf, sizeof(buf), "%u:%02u %u/%u/%u", d[4], d[5], d[3], d[2], d[0] | d[1] << 8);
      this->publish_state(buf);
      return;
    }
    int64_t v = this->value(d);
    for (auto &option : this->options_) {
      if (option.first == v) {
        this->publish_state(option.second);
        return;
      }
    }
    snprintf(buf, sizeof(buf), "%" PRId64, v);
    this->publish_state(buf);
  }
  void invalidate() override { OPTOLINK_UNKNOWN(text_sensor); }

 protected:
  std::vector<std::pair<int32_t, const char *>> options_;
};
#endif

#ifdef USE_NUMBER
class OptolinkNumber : public number::Number, public Datapoint {
 public:
  void set_divisor(float divisor) { this->divisor_ = divisor; }
  void publish(const uint8_t *data) override { this->publish_state(this->value(data) / this->divisor_); }
  void invalidate() override { OPTOLINK_UNKNOWN(number); }

 protected:
  void control(float value) override {
    int32_t raw = lroundf(value * this->divisor_);
    this->publish_state(raw / this->divisor_);
    this->write(raw);
  }
  float divisor_{1};
};
#endif

#ifdef USE_SELECT
// Option i is the raw value values_[i].
class OptolinkSelect : public select::Select, public Datapoint {
 public:
  void set_values(std::vector<int32_t> values) { this->values_ = std::move(values); }
  void publish(const uint8_t *data) override {
    int64_t v = this->value(data);
    for (size_t i = 0; i < this->values_.size(); i++) {
      if (this->values_[i] == v) {
        this->publish_state(i);
        return;
      }
    }
    ESP_LOGW("optolink", "%s: value %" PRId64 " is not an option", this->get_name().c_str(), v);
  }
  void invalidate() override { OPTOLINK_UNKNOWN(select); }

 protected:
  void control(size_t index) override {
    this->publish_state(index);
    this->write(this->values_[index]);
  }
  std::vector<int32_t> values_;
};
#endif

#ifdef USE_SWITCH
// On unless the value is off_value; turning on writes back the last value seen while on,
// kept in flash: after a reboot with the switch off it is still known.
class OptolinkSwitch : public switch_::Switch, public Datapoint {
 public:
  void set_values(int32_t on, int32_t off) {
    this->on_ = on;
    this->off_ = off;
  }
  void publish(const uint8_t *data) override {
    int64_t v = this->value(data);
    if (v != this->off_ && v != this->on_value_()) {
      this->on_ = v;
      this->pref_.save(&this->on_);
    }
    this->publish_state(v != this->off_);
  }
  void invalidate() override {
    this->publish_dedup_.next_unknown();  // the next read is published even if unchanged
    OPTOLINK_UNKNOWN(switch);
  }

 protected:
  void write_state(bool state) override {
    this->publish_state(state);
    this->write(state ? this->on_value_() : this->off_);
  }
  int32_t on_value_() {  // restored from flash on first use: preferences aren't ready at construction
    if (!this->restored_) {
      this->restored_ = true;
      // own key (same as before 2026.10: entity hash ^ version): Switch keeps its state under version 0
      this->pref_ = this->make_entity_preference<int32_t>(0x6F6E7631);
      this->pref_.load(&this->on_);
    }
    return this->on_;
  }
  int32_t on_{1};
  int32_t off_{0};
  bool restored_{false};
  ESPPreferenceObject pref_;
};
#endif

}  // namespace optolink
}  // namespace esphome

#undef OPTOLINK_UNKNOWN
