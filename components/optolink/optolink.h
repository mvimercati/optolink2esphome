#pragma once
// Viessmann boiler on the Optolink (P300): polls the datapoints declared in the YAML
// (sensor, text_sensor, number, select, switch with platform optolink) and writes them.
//
// One transaction at a time and never blocking loop(): a request is sent, the reply is
// collected across loop() calls until it is complete or times out. Writes go first; after
// a write the datapoints covering that address are read again. A write of the value last read
// is skipped (the settings are likely in EEPROM, which wears out). A read serves every
// datapoint it covers. Three failures in a row (or an ENQ: the boiler fell back to KW) and the
// link is set up again. A boiler that was silent (off) answers garbage for a while after power
// on (0 °C outside, 255 from the solar module, counters at 0): what it reads in the first
// WARMUP is dropped, then everything is read again.

#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

#include "protocol.h"

namespace esphome {
namespace optolink {

class Optolink;

// A value at an address of the boiler. Subclasses publish it to their entity.
class Datapoint {
 public:
  void configure(Optolink *parent, uint16_t address, Type type, uint8_t offset, uint32_t interval);
  uint8_t length() const { return this->offset_ + type_size(this->type_); }  // bytes read
  virtual void publish(const uint8_t *data) = 0;  // data: the value bytes (offset applied)
  virtual void invalidate() = 0;                  // the link is down: unknown in Home Assistant

 protected:
  friend class Optolink;
  int64_t value(const uint8_t *data) const { return decode(this->type_, data); }
  void write(int32_t value);

  Optolink *parent_{nullptr};
  uint16_t address_{0};
  Type type_{U8};
  uint8_t offset_{0};
  uint32_t interval_{0};
  uint32_t next_{0};  // millis() of the next read
  int64_t last_{0};   // the value last read, if known_ (not written since, link up)
  bool known_{false};
};

class Optolink : public Component, public uart::UARTDevice {
 public:
  void add(Datapoint *dp) { this->points_.push_back(dp); }
  void write(uint16_t address, Type type, int32_t value);
  void set_timeout(uint32_t ms) { this->timeout_ = ms; }
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_errors_sensor(sensor::Sensor *s) { this->errors_sensor_ = s; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  enum class State : uint8_t { WAIT_ENQ, WAIT_SYNC, IDLE, BUSY };
  struct Write {
    uint16_t address;
    uint8_t len;
    uint8_t data[4];
  };

  void reset_();
  void start_(uint32_t now);
  void receive_(uint32_t now);
  void done_(uint32_t now, Reply r);
  void set_connected_(bool connected);
  int byte_();  // the next received byte, -1 if none
  void send_(const uint8_t *data, size_t len, State next);

  std::vector<Datapoint *> points_;
  std::vector<Write> writes_;
  State state_{State::WAIT_ENQ};
  uint8_t tx_[MAX_FRAME];
  uint8_t rx_[MAX_FRAME];
  size_t rx_len_{0};
  uint32_t sent_at_{0};
  uint32_t quiet_until_{0};
  uint32_t timeout_{2000};
  uint8_t failures_{0};  // in a row
  uint32_t errors_{0};   // since boot
  bool connected_{false};
  uint8_t silences_{0};  // ENQ timeouts since the last link up (the first one at boot: USB not ready)
  bool warming_{false};  // until warm_until_: the boiler is starting up, replies are dropped
  uint32_t warm_until_{0};
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  sensor::Sensor *errors_sensor_{nullptr};
};

}  // namespace optolink
}  // namespace esphome
