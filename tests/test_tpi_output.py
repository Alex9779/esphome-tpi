"""Compile the actual component against a deterministic clock and ESPHome stubs."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUBS = {
    "esphome/core/component.h": """
#pragma once
#include <cstdint>
#include <algorithm>
extern uint32_t test_now;
namespace esphome {
inline uint32_t millis() { return test_now; }
template<class T> T clamp(T x, T low, T high) { return std::max(low, std::min(x, high)); }
class Component { public: virtual void setup() {} virtual void loop() {} virtual void dump_config() {} };
}
""",
    "esphome/components/output/float_output.h": """
#pragma once
namespace esphome::output {
class FloatOutput { protected: virtual void write_state(float) = 0; };
}
""",
    "esphome/components/output/binary_output.h": """
#pragma once
namespace esphome::output {
class BinaryOutput { public: bool state{false}; void set_state(bool value) { state = value; } };
}
""",
    "esphome/components/binary_sensor/binary_sensor.h": """
#pragma once
namespace esphome::binary_sensor { class BinarySensor { public: bool state{false}; }; }
""",
    "esphome/core/time.h": """
#pragma once
namespace esphome { struct ESPTime {
int hour{12}, minute{0}, second{0}; bool is_valid() const { return true; }
}; }
""",
    "esphome/components/time/real_time_clock.h": """
#pragma once
#include "esphome/core/time.h"
namespace esphome::time { class RealTimeClock { public: ESPTime now() { return {}; } }; }
""",
    "esphome/components/datetime/datetime_base.h": """
#pragma once
#include "esphome/core/time.h"
namespace esphome::datetime { class DateTimeBase { public:
bool has_state() { return true; } ESPTime state_as_esptime() { return {}; }
}; }
""",
    "esphome/core/log.h": """
#pragma once
#define ESP_LOGCONFIG(...) ((void)0)
#define ESP_LOGV(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
""",
}
HARNESS = r'''
#include "tpi_output.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
uint32_t test_now = 0;
class TestTPI : public esphome::tpi::TPIOutput {
 public:
  using TPIOutput::write_state;
  double balance() { return this->time_balance_; }
};
struct Fixture {
  TestTPI tpi;
  esphome::output::BinaryOutput pin;
  esphome::binary_sensor::BinarySensor night;
  std::vector<uint32_t> transitions;
  Fixture(uint32_t start = 0) {
    test_now = start;
    tpi.set_binary_output(&pin);
    tpi.set_period(1800000);
    tpi.set_min_on_time(600000);
    tpi.set_min_off_time(600000);
    tpi.set_night_off_sensor(&night);
    tpi.setup();
  }
  void tick(uint32_t duration) {
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 1000) {
      test_now += 1000;
      bool old = pin.state;
      tpi.loop();
      if (old != pin.state) transitions.push_back(test_now);
    }
  }
  void demand(float value) { tpi.write_state(value); tpi.loop(); }
  void ready(float value) { tick(600000); demand(value); assert(pin.state); }
};
int main() {
  // Low demand keeps the full ten-minute pulse and repays it over subsequent OFF time.
  for (float duty : {0.01f, 0.1f, 0.2f, 0.5f, 0.8f, 0.99f}) {
    Fixture f;
    f.ready(duty);
    uint64_t on_ms = 0;
    const uint32_t duration = 36000000; // ten hours, with actual component loop calls
    for (uint32_t elapsed = 0; elapsed < duration; elapsed += 1000) {
      if (f.pin.state) on_ms += 1000;
      f.tick(1000);
    }
    assert(std::abs(static_cast<double>(on_ms) - duty * duration) <= 1802000);
    assert(std::abs(f.tpi.balance()) <= 1800000);
    for (size_t i = 1; i < f.transitions.size(); ++i)
      assert(f.transitions[i] - f.transitions[i - 1] >= 600000);
    if (duty == 0.1f) {
      assert(f.transitions[0] == 1200000);
      assert(f.transitions[1] >= 6599000 && f.transitions[1] <= 6601000); // 90 minutes OFF
    }
    if (duty == 0.2f) {
      assert(f.transitions[0] == 1200000); // first pulse: 10 minutes
      assert(f.transitions[1] >= 3599000 && f.transitions[1] <= 3601000); // 40 minutes OFF
    }
  }
  {
    Fixture f; f.ready(0.2f); f.tick(600000); assert(!f.pin.state);
    f.tick(600000); f.demand(1.0f); assert(f.pin.state); // full demand bypasses repayment
  }
  {
    Fixture f; f.tick(600000); f.demand(0.2f);
    test_now += 60000; f.demand(0.8f);
    assert(std::abs(f.tpi.balance() + 48000) < 1); // old interval uses old demand
  }
  {
    Fixture f; f.ready(0.2f); f.tick(600000); assert(!f.pin.state);
    f.tick(600000); f.demand(0.8f);
    f.tick(450000); assert(f.pin.state); // increased demand repays debt faster
  }
  {
    Fixture f; f.ready(0.2f); f.tick(300000); f.demand(0.8f);
    f.tick(900000); assert(f.pin.state); // active pulse extends as demand rises
    f.demand(0.1f); assert(!f.pin.state); // may stop once minimum ON time elapsed
  }
  {
    Fixture f; f.ready(0.2f); f.tick(120000); f.demand(0.0f);
    assert(f.pin.state); f.tick(480000); assert(!f.pin.state);
    assert(f.tpi.balance() < 0); // delivered excess is retained across cancellation
    f.tick(600000); f.demand(0.2f); assert(!f.pin.state);
  }
  {
    Fixture f; f.demand(1.0f); f.tick(599000); assert(!f.pin.state);
    f.tick(1000); assert(f.pin.state); f.tick(3600000); assert(f.pin.state);
    f.demand(std::numeric_limits<float>::quiet_NaN()); assert(!f.pin.state);
  }
  {
    Fixture f; f.ready(0.2f); f.tick(120000); f.night.state = true; f.tpi.loop();
    assert(!f.pin.state); assert(f.tpi.balance() == 0);
    f.tick(3600000); assert(f.tpi.balance() == 0);
    f.night.state = false; f.tpi.loop(); assert(f.pin.state);
  }
  {
    Fixture f; f.ready(0.2f); f.night.state = true; f.tpi.loop();
    f.tick(300000); f.night.state = false; f.tpi.loop(); assert(!f.pin.state);
    f.tick(300000); assert(f.pin.state); // minimum OFF time survives short shutdown
  }
  {
    Fixture f(0xffff0000U); f.ready(0.2f); f.tick(600000); assert(!f.pin.state);
    f.tick(2401000); assert(f.pin.state); // millis rollover preserves elapsed intervals
  }
  {
    Fixture f; f.ready(0.2f); test_now += 20000000; f.tpi.loop();
    assert(std::abs(f.tpi.balance()) <= 1800000); // long delayed loop bounds history
  }
  std::cout << "PASS: duty balance, minimum times, demand changes, zero/NaN, night shutdown, rollover, bounded history\n";
}
'''


def main():
    compiler = shutil.which("clang++") or shutil.which("g++")
    if compiler is None:
        raise SystemExit("A C++ compiler is required")
    with tempfile.TemporaryDirectory(prefix="tpi-tests-") as directory:
        temp = Path(directory)
        for relative, text in STUBS.items():
            path = temp / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)
        harness = temp / "test.cpp"
        harness.write_text(HARNESS)
        for defines in ([], ["-DUSE_DATETIME"]):
            binary = temp / "test"
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-variable", *defines, "-I", str(temp),
                "-I", str(ROOT / "components/tpi"), str(harness),
                str(ROOT / "components/tpi/tpi_output.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
