#pragma once

#include "esphome/core/component.h"
#include "esphome/components/output/float_output.h"
#include "esphome/components/output/binary_output.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/time.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/datetime/datetime_base.h"

namespace esphome {
namespace tpi {

class TPIOutput : public output::FloatOutput, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void loop() override;

  void set_binary_output(output::BinaryOutput *binary_output) { binary_output_ = binary_output; }
  void set_period(uint32_t period) { period_ = period; }
  void set_min_on_time(uint32_t min_on_time) { min_on_time_ = min_on_time; }
  void set_min_off_time(uint32_t min_off_time) { min_off_time_ = min_off_time; }
  void set_night_off_sensor(binary_sensor::BinarySensor *sensor) { night_off_sensor_ = sensor; }
  void set_time(time::RealTimeClock *time) { time_ = time; }
  void set_night_off_start(uint8_t hour, uint8_t minute, uint8_t second) {
    night_off_start_hour_ = hour;
    night_off_start_minute_ = minute;
    night_off_start_second_ = second;
    use_night_off_time_ = true;
  }
  void set_night_off_end(uint8_t hour, uint8_t minute, uint8_t second) {
    night_off_end_hour_ = hour;
    night_off_end_minute_ = minute;
    night_off_end_second_ = second;
  }
  void set_night_off_datetime_start(datetime::DateTimeBase *dt) { 
    night_off_datetime_start_ = dt;
    use_night_off_datetime_ = true;
  }
  void set_night_off_datetime_end(datetime::DateTimeBase *dt) { 
    night_off_datetime_end_ = dt;
  }

 protected:
  void write_state(float state) override;
  void update_output_();
  bool is_night_off_active_();

  output::BinaryOutput *binary_output_{nullptr};
  uint32_t period_{10000};      // Default 10 seconds
  uint32_t min_on_time_{0};     // Minimum time output must stay on
  uint32_t min_off_time_{0};    // Minimum time output must stay off
  
  float target_value_{0.0f};    // Target duty cycle (0.0 - 1.0)
  bool output_state_{false};    // Current output state
  uint32_t cycle_start_time_{0}; // When current cycle started
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
  
  // Night off datetime components
  bool use_night_off_datetime_{false};
  datetime::DateTimeBase *night_off_datetime_start_{nullptr};
  datetime::DateTimeBase *night_off_datetime_end_{nullptr};
};

}  // namespace tpi
}  // namespace esphome
