#include "tpi_output.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#ifdef USE_DATETIME
#include "esphome/components/datetime/datetime_base.h"
#endif

namespace esphome::tpi {

static const char *const TAG = "tpi.output";

void TPIOutput::setup() {
  this->last_account_time_ = millis();
  this->time_balance_ = 0.0;
  this->night_off_active_ = false;
  this->last_change_time_ = millis();
  this->output_state_ = false;

  if (this->binary_output_ != nullptr) {
    this->binary_output_->set_state(false);
  }
}

void TPIOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "TPI Output (time-balance scheduling):");
  ESP_LOGCONFIG(TAG, "  Period: %u ms", this->period_);
  ESP_LOGCONFIG(TAG, "  Min ON time: %u ms", this->min_on_time_);
  ESP_LOGCONFIG(TAG, "  Min OFF time: %u ms", this->min_off_time_);
  if (this->night_off_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Night off sensor: configured");
  }
#ifdef USE_DATETIME
  if (this->use_night_off_datetime_) {
    ESP_LOGCONFIG(TAG, "  Night off datetime: configured (using datetime components)");
  } else
#endif
  if (this->use_night_off_time_) {
    ESP_LOGCONFIG(TAG, "  Night off time: %02d:%02d:%02d - %02d:%02d:%02d",
                  this->night_off_start_hour_, this->night_off_start_minute_, this->night_off_start_second_,
                  this->night_off_end_hour_, this->night_off_end_minute_, this->night_off_end_second_);
  }
}

void TPIOutput::account_time_(uint32_t now) {
  const uint32_t elapsed = now - this->last_account_time_;
  this->last_account_time_ = now;
  if (this->night_off_active_)
    return;

  this->time_balance_ += (static_cast<double>(this->target_value_) - (this->output_state_ ? 1.0 : 0.0)) * elapsed;
  // Bound history: a delayed loop or a long minimum time must not create an unlimited backlog.
  const double limit = std::max(this->period_, std::max(this->min_on_time_, this->min_off_time_));
  this->time_balance_ = std::max(-limit, std::min(limit, this->time_balance_));
}

void TPIOutput::set_output_state_(bool state, uint32_t now) {
  if (state == this->output_state_)
    return;
  this->output_state_ = state;
  this->last_change_time_ = now;
  this->binary_output_->set_state(state);
  ESP_LOGV(TAG, "State change to %s (target: %.3f, time balance: %.0f ms)",
           state ? "ON" : "OFF", this->target_value_, this->time_balance_);
}

void TPIOutput::write_state(float state) {
  // Account the elapsed interval with the previous demand before replacing it.
  this->account_time_(millis());
  this->target_value_ = std::isfinite(state) ? clamp(state, 0.0f, 1.0f) : 0.0f;
  // Discard undelivered demand when heating is cancelled, but retain excess already delivered.
  if (this->target_value_ == 0.0f)
    this->time_balance_ = std::min(0.0, this->time_balance_);
}

void TPIOutput::loop() {
  if (this->binary_output_ == nullptr)
    return;

  const uint32_t now = millis();
  this->account_time_(now);
  const bool night_off = this->is_night_off_active_();
  if (night_off) {
    this->set_output_state_(false, now);
    this->time_balance_ = 0.0;
    this->night_off_active_ = true;
    return;
  }
  if (this->night_off_active_) {
    // Never carry a demand or repayment obligation across a configured night shutdown.
    this->night_off_active_ = false;
    this->time_balance_ = 0.0;
  }

  const uint32_t time_since_change = now - this->last_change_time_;
  if (this->output_state_) {
    // Normal pulses track demand * period. Short pulses are advanced to min_on_time and repaid while OFF.
    const double requested_on_time = static_cast<double>(this->target_value_) * this->period_;
    const double pulse_time = std::max(static_cast<double>(this->min_on_time_), requested_on_time);
    if (this->target_value_ < 1.0f && time_since_change >= pulse_time && this->time_balance_ <= 0.0)
      this->set_output_state_(false, now);
  } else if (this->target_value_ > 0.0f && time_since_change >= this->min_off_time_ &&
             (this->time_balance_ >= 0.0 || this->target_value_ >= 1.0f)) {
    this->set_output_state_(true, now);
  }
}

bool TPIOutput::is_night_off_active_() {
  // Check binary sensor first
  if (this->night_off_sensor_ != nullptr) {
    return this->night_off_sensor_->state;
  }

#ifdef USE_DATETIME
  // Check datetime components
  if (this->use_night_off_datetime_) {
    if (this->night_off_datetime_start_ == nullptr || this->night_off_datetime_end_ == nullptr) {
      ESP_LOGD(TAG, "Night off datetime: components are null");
      return false;
    }

    // Require time component for datetime-based night off
    if (this->time_ == nullptr) {
      ESP_LOGD(TAG, "Night off datetime: time component is null");
      return false;
    }

    // Check if datetime entities have a state
    if (!this->night_off_datetime_start_->has_state() || !this->night_off_datetime_end_->has_state()) {
      ESP_LOGD(TAG, "Night off datetime: datetime entities have no state (start: %d, end: %d)",
               this->night_off_datetime_start_->has_state(),
               this->night_off_datetime_end_->has_state());
      return false; // If datetime not set, don't block output
    }

    // Get current time
    ESPTime now = this->time_->now();
    if (!now.is_valid()) {
      ESP_LOGD(TAG, "Night off datetime: current time not valid");
      return false;
    }

    // Get datetime state from components - for TIME entities, only hour/minute/second are relevant
    auto start_state = this->night_off_datetime_start_->state_as_esptime();
    auto end_state = this->night_off_datetime_end_->state_as_esptime();

    uint32_t now_seconds = now.hour * 3600 + now.minute * 60 + now.second;
    uint32_t start_seconds = start_state.hour * 3600 + start_state.minute * 60 + start_state.second;
    uint32_t end_seconds = end_state.hour * 3600 + end_state.minute * 60 + end_state.second;

    ESP_LOGD(TAG, "Night off datetime check: now=%02d:%02d:%02d (%u), start=%02d:%02d:%02d (%u), end=%02d:%02d:%02d (%u)",
             now.hour, now.minute, now.second, now_seconds,
             start_state.hour, start_state.minute, start_state.second, start_seconds,
             end_state.hour, end_state.minute, end_state.second, end_seconds);

    // Handle wrap around midnight
    if (start_seconds > end_seconds) {
      bool active = now_seconds >= start_seconds || now_seconds < end_seconds;
      ESP_LOGD(TAG, "Night off datetime (wrap): %s", active ? "ACTIVE" : "INACTIVE");
      return active;
    } else {
      bool active = now_seconds >= start_seconds && now_seconds < end_seconds;
      ESP_LOGD(TAG, "Night off datetime (no wrap): %s", active ? "ACTIVE" : "INACTIVE");
      return active;
    }
  }
#endif  // USE_DATETIME

  // Check time interval
  if (this->use_night_off_time_) {
    if (this->time_ == nullptr) {
      return false; // If time component not configured, don't block output
    }

    ESPTime now = this->time_->now();
    if (!now.is_valid()) {
      return false; // If time not available, don't block output
    }

    uint32_t now_seconds = now.hour * 3600 + now.minute * 60 + now.second;
    uint32_t start_seconds = this->night_off_start_hour_ * 3600 +
                             this->night_off_start_minute_ * 60 +
                             this->night_off_start_second_;
    uint32_t end_seconds = this->night_off_end_hour_ * 3600 +
                           this->night_off_end_minute_ * 60 +
                           this->night_off_end_second_;

    // Handle wrap around midnight
    if (start_seconds > end_seconds) {
      return now_seconds >= start_seconds || now_seconds < end_seconds;
    } else {
      return now_seconds >= start_seconds && now_seconds < end_seconds;
    }
  }

  return false;
}

}  // namespace esphome::tpi
