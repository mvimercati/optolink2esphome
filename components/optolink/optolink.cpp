#include "optolink.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace optolink {

static const char *const TAG = "optolink";
static constexpr uint32_t ENQ_TIMEOUT = 5000;  // in KW mode the boiler sends ENQ about every 2 s
static constexpr uint32_t SETTLE = 500;        // after a failure, before the next request
static constexpr uint8_t MAX_FAILURES = 3;
static constexpr uint8_t SILENT = 2;           // ENQ timeouts between link ups: the boiler was off
static constexpr uint32_t WARMUP = 90000;      // after power on, until the boiler's values are real

static bool reached(uint32_t now, uint32_t t) { return int32_t(now - t) >= 0; }

void Datapoint::configure(Optolink *parent, uint16_t address, Type type, uint8_t offset, uint32_t interval) {
  this->parent_ = parent;
  this->address_ = address;
  this->type_ = type;
  this->offset_ = offset;
  this->interval_ = interval;
  parent->add(this);
}

void Datapoint::write(int32_t value) { this->parent_->write(this->address_ + this->offset_, this->type_, value); }

void Optolink::write(uint16_t address, Type type, int32_t value) {
  Write w{address, type_size(type), {}};
  encode(type, value, w.data);
  for (auto &pending : this->writes_) {
    if (pending.address == address) {
      ESP_LOGI(TAG, "Write 0x%04X = %" PRId32, address, value);
      pending = w;  // not sent yet: the latest value wins
      return;
    }
  }
  for (auto *dp : this->points_) {
    if (dp->known_ && dp->address_ + dp->offset_ == address && dp->type_ == type && dp->last_ == value) {
      ESP_LOGD(TAG, "Write 0x%04X = %" PRId32 " skipped: already set", address, value);
      return;
    }
  }
  ESP_LOGI(TAG, "Write 0x%04X = %" PRId32, address, value);
  for (auto *dp : this->points_) {  // unknown until read back after the write
    if (dp->address_ < address + w.len && address < dp->address_ + dp->length())
      dp->known_ = false;
  }
  this->writes_.push_back(w);
}

void Optolink::setup() {
  if (this->connected_sensor_ != nullptr)
    this->connected_sensor_->publish_initial_state(false);
  if (this->errors_sensor_ != nullptr)
    this->errors_sensor_->publish_state(0);
  this->reset_();
}

void Optolink::dump_config() {
  ESP_LOGCONFIG(TAG, "Optolink (P300): %u datapoints, timeout %" PRIu32 " ms", unsigned(this->points_.size()),
                this->timeout_);
}

void Optolink::loop() {
  uint32_t now = millis();
  if (!reached(now, this->quiet_until_))
    return;
  int byte;
  switch (this->state_) {
    case State::WAIT_ENQ:
      while ((byte = this->byte_()) >= 0) {
        if (byte == ENQ) {
          this->send_(SYNC, sizeof(SYNC), State::WAIT_SYNC);
          return;
        }
      }
      if (now - this->sent_at_ > ENQ_TIMEOUT) {
        if (this->silences_ < SILENT)
          this->silences_++;
        this->reset_();
      }
      break;
    case State::WAIT_SYNC:
      while ((byte = this->byte_()) == ENQ) {  // a late ENQ may cross our SYNC
      }
      if (byte == ACK) {
        this->state_ = State::IDLE;
        this->set_connected_(true);
        if (this->silences_ >= SILENT) {
          ESP_LOGI(TAG, "The boiler is starting up: values dropped for %" PRIu32 " s", WARMUP / 1000);
          this->warming_ = true;
          this->warm_until_ = now + WARMUP;
        }
        this->silences_ = 0;
      } else if (byte >= 0 || now - this->sent_at_ > this->timeout_) {
        this->reset_();
      }
      break;
    case State::IDLE:
      while ((byte = this->byte_()) >= 0) {
        if (byte == ENQ) {
          ESP_LOGW(TAG, "The boiler fell back to KW mode");
          this->reset_();
          return;
        }
      }
      this->start_(now);
      break;
    case State::BUSY:
      this->receive_(now);
      break;
  }
}

// Sends the next write, or reads the most overdue datapoint.
void Optolink::start_(uint32_t now) {
  if (this->warming_ && reached(now, this->warm_until_)) {
    ESP_LOGI(TAG, "Boiler started: reading everything");
    this->warming_ = false;
    for (auto *dp : this->points_)
      dp->next_ = now;
  }
  size_t len;
  if (!this->writes_.empty()) {
    Write w = this->writes_.front();
    this->writes_.erase(this->writes_.begin());
    len = request(this->tx_, w.address, w.len, w.data);
  } else {
    Datapoint *due = nullptr;
    for (auto *dp : this->points_) {
      if (reached(now, dp->next_) && (due == nullptr || int32_t(dp->next_ - due->next_) < 0))
        due = dp;
    }
    if (due == nullptr)
      return;
    len = request(this->tx_, due->address_, due->length());
  }
  this->rx_len_ = 0;
  this->send_(this->tx_, len, State::BUSY);
}

void Optolink::receive_(uint32_t now) {
  Reply r = INCOMPLETE;
  int byte;
  while (r == INCOMPLETE && (byte = this->byte_()) >= 0) {
    this->rx_[this->rx_len_++] = byte;
    r = reply(this->tx_, this->rx_, this->rx_len_);
  }
  if (r != INCOMPLETE || now - this->sent_at_ > this->timeout_)
    this->done_(now, r);
}

void Optolink::done_(uint32_t now, Reply r) {
  this->state_ = State::IDLE;
  bool read = this->tx_[3] == READ;
  uint16_t address = this->tx_[4] << 8 | this->tx_[5];
  uint8_t n = this->tx_[6];
  // A read serves every datapoint it covers (a number and a switch on one register are read once);
  // a write makes the datapoints it touched due, failed or not.
  for (auto *dp : this->points_) {
    if (read && dp->address_ >= address && dp->address_ + dp->length() <= address + n) {
      dp->next_ = now + dp->interval_;
      if (r == OK && !this->warming_) {
        const uint8_t *data = this->rx_ + 8 + (dp->address_ - address) + dp->offset_;
        dp->last_ = dp->value(data);
        dp->known_ = true;
        dp->publish(data);
      }
    } else if (!read && address >= dp->address_ && address < dp->address_ + dp->length()) {
      dp->next_ = now;
    }
  }
  if (r == OK) {
    this->failures_ = 0;
    return;
  }
  if (this->errors_sensor_ != nullptr)
    this->errors_sensor_->publish_state(++this->errors_);
  const char *op = read ? "Read" : "Write";
  if (r == ERROR) {  // the link is fine, the boiler refused the request
    ESP_LOGW(TAG, "%s 0x%04X refused by the boiler", op, address);
    return;
  }
  char hex[format_hex_pretty_size(MAX_FRAME)];
  ESP_LOGW(TAG, "%s 0x%04X failed (%s): %s", op, address, r == BAD ? "bad reply" : "timeout",
           format_hex_pretty_to(hex, this->rx_, this->rx_len_));
  this->quiet_until_ = now + SETTLE;
  if (++this->failures_ >= MAX_FAILURES || (this->rx_len_ > 0 && this->rx_[0] == ENQ))
    this->reset_();
}

// EOT puts the boiler back in KW mode; its next ENQ starts the P300 setup.
void Optolink::reset_() {
  ESP_LOGV(TAG, "Setting up the link");
  this->set_connected_(false);
  this->failures_ = 0;
  this->send_(&EOT, 1, State::WAIT_ENQ);
}

void Optolink::set_connected_(bool connected) {
  if (connected == this->connected_)
    return;
  this->connected_ = connected;
  if (connected) {
    ESP_LOGI(TAG, "Link up");
  } else {
    ESP_LOGW(TAG, "Link down");
    for (auto *dp : this->points_) {
      dp->known_ = false;
      dp->invalidate();
    }
  }
  if (this->connected_sensor_ != nullptr)
    this->connected_sensor_->publish_state(connected);
}

int Optolink::byte_() {
  uint8_t byte;
  return this->available() && this->read_byte(&byte) ? byte : -1;
}

void Optolink::send_(const uint8_t *data, size_t len, State next) {
  while (this->byte_() >= 0) {
  }
  this->write_array(data, len);
  this->sent_at_ = millis();
  this->state_ = next;
}

}  // namespace optolink
}  // namespace esphome
