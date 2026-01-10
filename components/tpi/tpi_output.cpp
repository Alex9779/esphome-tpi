#include "tpi_output.h"
#include "esphome/core/log.h"

namespace esphome {
namespace tpi {

static const char *const TAG = "tpi.output";

void TPIOutput::setup() {
  this->cycle_start_time_ = millis();
  this->last_change_time_ = millis();
  this->output_state_ = false;
  
  if (this->binary_output_ != nullptr) {
    this->binary_output_->set_state(false);
  }
}

void TPIOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "TPI Output:");
  ESP_LOGCONFIG(TAG, "  Period: %u ms", this->period_);
  ESP_LOGCONFIG(TAG, "  Min ON time: %u ms", this->min_on_time_);
  ESP_LOGCONFIG(TAG, "  Min OFF time: %u ms", this->min_off_time_);
  if (this->night_off_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Night off sensor: configured");
  }
  if (this->use_night_off_datetime_) {
    ESP_LOGCONFIG(TAG, "  Night off datetime: configured (using datetime components)");
  } else if (this->use_night_off_time_) {
    ESP_LOGCONFIG(TAG, "  Night off time: %02d:%02d:%02d - %02d:%02d:%02d",
                  this->night_off_start_hour_, this->night_off_start_minute_, this->night_off_start_second_,
                  this->night_off_end_hour_, this->night_off_end_minute_, this->night_off_end_second_);
  }
}

void TPIOutput::write_state(float state) {
  // Clamp state between 0 and 1
  this->target_value_ = clamp(state, 0.0f, 1.0f);
}

void TPIOutput::loop() {
  if (this->binary_output_ == nullptr) {
    return;
  }

  // Check if night off is active
  if (this->is_night_off_active_()) {
    if (this->output_state_) {
      this->output_state_ = false;
      this->binary_output_->set_state(false);
      ESP_LOGV(TAG, "Night off active - output forced OFF");
    }
    return;
  }

  uint32_t now = millis();
  uint32_t time_in_cycle = now - this->cycle_start_time_;
  uint32_t time_since_change = now - this->last_change_time_;

  // Check if we've completed a full period
  if (time_in_cycle >= this->period_) {
    // Start a new cycle
    this->cycle_start_time_ = now;
    time_in_cycle = 0;
    
    // Decide initial state for new cycle based on target value
    if (this->target_value_ > 0.0f) {
      if (!this->output_state_) {
        this->output_state_ = true;
        this->binary_output_->set_state(true);
        this->last_change_time_ = now;
        ESP_LOGV(TAG, "New cycle: ON (target: %.2f)", this->target_value_);
      }
    } else {
      if (this->output_state_) {
        this->output_state_ = false;
        this->binary_output_->set_state(false);
        this->last_change_time_ = now;
        ESP_LOGV(TAG, "New cycle: OFF (target: %.2f)", this->target_value_);
      }
    }
  }

  // Calculate when we should switch based on duty cycle
  uint32_t on_time = (uint32_t)(this->target_value_ * this->period_);
  
  // Apply minimum on/off time constraints
  // If on_time is too short to meet min_on_time, turn off completely (0% output)
  if (on_time > 0 && on_time < this->min_on_time_) {
    on_time = 0;
  }
  // If off_time would be too short to meet min_off_time, stay on completely (100% output)
  if (on_time > 0 && on_time < this->period_ && (this->period_ - on_time) < this->min_off_time_) {
    on_time = this->period_;
  }
  
  // Ensure we don't exceed the period
  if (on_time > this->period_) {
    on_time = this->period_;
  }

  // Determine desired state based on time in cycle
  bool desired_state = (time_in_cycle < on_time) && (this->target_value_ > 0.0f);

  // Apply minimum time constraints before allowing state change
  if (desired_state != this->output_state_) {
    bool can_change = false;
    
    if (this->output_state_ && time_since_change >= this->min_on_time_) {
      // Currently ON and min on time has elapsed
      can_change = true;
    } else if (!this->output_state_ && time_since_change >= this->min_off_time_) {
      // Currently OFF and min off time has elapsed
      can_change = true;
    }
    
    if (can_change) {
      this->output_state_ = desired_state;
      this->binary_output_->set_state(desired_state);
      this->last_change_time_ = now;
      ESP_LOGV(TAG, "State change to %s (target: %.2f, time in cycle: %u ms, on_time: %u ms)", 
               desired_state ? "ON" : "OFF", this->target_value_, time_in_cycle, on_time);
    }
  }
}

bool TPIOutput::is_night_off_active_() {
  // Check binary sensor first
  if (this->night_off_sensor_ != nullptr) {
    return this->night_off_sensor_->state;
  }
  
  // Check datetime components
  if (this->use_night_off_datetime_) {
    if (this->night_off_datetime_start_ == nullptr || this->night_off_datetime_end_ == nullptr) {
      return false;
    }
    
    // Require time component for datetime-based night off
    if (this->time_ == nullptr) {
      return false;
    }
    
    // Get datetime state from components
    auto start_state = this->night_off_datetime_start_->state_as_esptime();
    auto end_state = this->night_off_datetime_end_->state_as_esptime();
    
    if (!start_state.is_valid() || !end_state.is_valid()) {
      return false; // If datetime not valid, don't block output
    }
    
    // Get current time
    ESPTime now = this->time_->now();
    if (!now.is_valid()) {
      return false;
    }
    
    uint32_t now_seconds = now.hour * 3600 + now.minute * 60 + now.second;
    uint32_t start_seconds = start_state.hour * 3600 + start_state.minute * 60 + start_state.second;
    uint32_t end_seconds = end_state.hour * 3600 + end_state.minute * 60 + end_state.second;
    
    // Handle wrap around midnight
    if (start_seconds > end_seconds) {
      return now_seconds >= start_seconds || now_seconds < end_seconds;
    } else {
      return now_seconds >= start_seconds && now_seconds < end_seconds;
    }
  }
  
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

}  // namespace tpi
}  // namespace esphome
