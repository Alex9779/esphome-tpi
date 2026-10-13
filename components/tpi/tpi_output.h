#pragma once

#include "esphome/core/component.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/output/binary_output.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/time.h"
#include "esphome/components/time/real_time_clock.h"

namespace esphome::datetime {
class DateTimeBase;
}

namespace esphome::tpi {

class TPIOutput : public output::FloatOutput, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;

  void set_binary_output(output::BinaryOutput *binary_output) { this->binary_output_ = binary_output; }
  void set_period(uint32_t period) { this->period_ = period; }
  void set_min_on_time(uint32_t min_on_time) { this->min_on_time_ = min_on_time; }
  void set_min_off_time(uint32_t min_off_time) { this->min_off_time_ = min_off_time; }
  void set_night_off_sensor(binary_sensor::BinarySensor *sensor) { this->night_off_sensor_ = sensor; }
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_night_off_start(uint8_t hour, uint8_t minute, uint8_t second) {
    this->night_off_start_hour_ = hour;
    this->night_off_start_minute_ = minute;
    this->night_off_start_second_ = second;
    this->use_night_off_time_ = true;
  }
  void set_night_off_end(uint8_t hour, uint8_t minute, uint8_t second) {
    this->night_off_end_hour_ = hour;
    this->night_off_end_minute_ = minute;
    this->night_off_end_second_ = second;
  }
#ifdef USE_DATETIME
  void set_night_off_datetime_start(datetime::DateTimeBase *dt) {
    this->night_off_datetime_start_ = dt;
    this->use_night_off_datetime_ = true;
  }
  void set_night_off_datetime_end(datetime::DateTimeBase *dt) {
    this->night_off_datetime_end_ = dt;
  }
#endif

 protected:
  void write_state(float state) override;
  void account_time_(uint32_t now);
  void set_output_state_(bool state, uint32_t now);
  bool is_night_off_active_();

  output::BinaryOutput *binary_output_{nullptr};
  uint32_t period_{1800000};   // Default 30 minutes
  uint32_t min_on_time_{600000};     // Minimum time output must stay on
  uint32_t min_off_time_{600000};    // Minimum time output must stay off

  float target_value_{0.0f};    // Target duty cycle (0.0 - 1.0)
  bool output_state_{false};    // Current output state
  double time_balance_{0.0};   // Requested minus delivered ON time, in milliseconds
  uint32_t last_account_time_{0};
  bool night_off_active_{false};
  uint32_t last_change_time_{0}; // When output last changed state

  // Night off configuration
  binary_sensor::BinarySensor *night_off_sensor_{nullptr};
  time::RealTimeClock *time_{nullptr};
  bool use_night_off_time_{false};
  uint8_t night_off_start_hour_{0};
  uint8_t night_off_start_minute_{0};
  uint8_t night_off_start_second_{0};
  uint8_t night_off_end_hour_{0};
  uint8_t night_off_end_minute_{0};
  uint8_t night_off_end_second_{0};

#ifdef USE_DATETIME
  // Night off datetime components
  bool use_night_off_datetime_{false};
  datetime::DateTimeBase *night_off_datetime_start_{nullptr};
  datetime::DateTimeBase *night_off_datetime_end_{nullptr};
#endif
};

}  // namespace esphome::tpi
