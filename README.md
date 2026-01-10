# ESPHome TPI (Time Proportional Integral) Output Component

A custom ESPHome component that provides Time Proportional Integral control for binary outputs. This component is particularly well-suited for controlling floor heating systems, where the slow thermal response benefits from long cycle times. The default values (30-minute period, 10-minute minimum on/off times) are optimized for typical floor heating installations.

## Features

- **Proportional Control**: Converts a float output (0.0-1.0) to time-proportional binary switching
- **Configurable Period**: Set the cycle time for the TPI algorithm
- **Minimum On/Off Times**: Prevent rapid switching by enforcing minimum state durations
- **Compatible with PID**: Works seamlessly with ESPHome's PID climate controller

## Installation

### Method 1: External Component (Recommended)

Add this to your ESPHome YAML configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/alex9779/esphome-tpi
    components: [ tpi ]
```

Or use a specific version/branch:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/alex9779/esphome-tpi
      ref: main
    refresh: 0s  # Check repository every time (0s = always check for updates)
    components: [ tpi ]
```

### Method 2: Local Copy

1. Copy the `components/tpi` folder to your ESPHome configuration directory
2. Reference it in your YAML configuration

## Configuration

```yaml
# Required for time-based night off (methods 2 and 3)
time:
  - platform: homeassistant
    id: homeassistant_time

output:
  # First, define the binary output you want to control
  - platform: gpio
    pin: GPIO5
    id: relay_output

  # Then configure the TPI output
  - platform: tpi
    id: tpi_heater
    output: relay_output         # Required: ID of the binary output to control
    period: 10s                   # Optional: TPI cycle period (default: 10s)
    min_on_time: 1s              # Optional: Minimum time output stays on (default: 0s)
    min_off_time: 1s             # Optional: Minimum time output stays off (default: 0s)
    # Optional night off - choose ONE method:
    # Method 1: Use a binary sensor
    night_off_sensor: night_mode_switch
    # Method 2: Use time interval with hardcoded times (wraps around midnight if start > end)
    # time_id: homeassistant_time  # Required for methods 2 and 3
    # night_off_start: "22:00:00"
    # night_off_end: "06:00:00"
    # Method 3: Use datetime component IDs for dynamic times
    # night_off_start: time_off
    # night_off_end: time_on
```

### Configuration Variables

- **output** (Required, ID): The ID of the binary output component that this TPI output will control
- **period** (Optional, Time): The cycle time for TPI control. Default is `30min`. Longer periods result in fewer switches but slower response
- **min_on_time** (Optional, Time): Minimum time the output must remain ON before switching OFF. Default is `10min`. Useful for protecting relays or heating elements
- **min_off_time** (Optional, Time): Minimum time the output must remain OFF before switching ON. Default is `10min`. Useful for protecting compressors or motors
- **night_off_sensor** (Optional, ID): Binary sensor that when ON, forces output OFF regardless of input. Cannot be used with `night_off_start`/`night_off_end`
- **night_off_start** (Optional, Time or ID): Start time for night off period. Can be either:
  - A hardcoded time string (format: `HH:MM:SS`)
  - An ID of a `datetime` component of type `time`
  Must be used with `night_off_end`. Cannot be used with `night_off_sensor`. **Requires `time_id` to be configured**
- **night_off_end** (Optional, Time or ID): End time for night off period. Can be either:
  - A hardcoded time string (format: `HH:MM:SS`)
  - An ID of a `datetime` component of type `time`
  Must be used with `night_off_start`. Cannot be used with `night_off_sensor`. **Requires `time_id` to be configured**
- **time_id** (Optional, ID): Reference to a `time` component (e.g., `homeassistant`, `sntp`, etc.). **Required** when using `night_off_start`/`night_off_end` (both hardcoded times and datetime component IDs need this to get the current time for comparison)

## How It Works

The TPI output divides time into fixed periods. Within each period, the output is turned ON for a duration proportional to the requested value (0.0-1.0):

- Value 0.0 = output OFF for entire period
- Value 0.5 = output ON for 50% of the period
- Value 1.0 = output ON for entire period

The minimum on/off times ensure that once the output switches state, it remains in that state for at least the specified duration, preventing excessive switching.

### Night Off Mode

The TPI output supports a "night off" feature that forces the output to remain OFF regardless of the control input. This is useful for scheduling heating/cooling systems to be inactive during certain periods. You can configure this in three ways:

1. **Binary Sensor**: The output stays OFF when the sensor is ON
2. **Time Interval (Hardcoded)**: The output stays OFF during a specific hardcoded time range (e.g., 22:00 to 06:00)
3. **Time Interval (Dynamic)**: The output stays OFF during a time range defined by datetime components that can be changed at runtime

The time interval automatically handles wrapping around midnight if the start time is later than the end time.

#### Example with DateTime Components

```yaml
# Define datetime components for adjustable times
datetime:
  - platform: template
    id: time_off
    type: time
    name: "Heating Off Time"
    optimistic: yes
    initial_value: "22:00"
    restore_value: true

  - platform: template
    id: time_on
    type: time
    name: "Heating On Time"
    optimistic: yes
    initial_value: "06:00"
    restore_value: true

# Reference them in TPI output
output:
  - platform: tpi
    id: tpi_heater
    output: relay_output
    night_off_start: time_off
    night_off_end: time_on
```

This allows users to adjust the heating schedule from Home Assistant without reflashing the device.

## Example Use Case: PID Climate Control

```yaml
climate:
  - platform: pid
    name: "Heater Controller"
    sensor: temperature_sensor
    default_target_temperature: 21°C
    heat_output: tpi_heater  # Reference the TPI output
    control_parameters:
      kp: 0.5
      ki: 0.001
      kd: 0.0
```

## Tips

- **Floor Heating Systems**: The default values work well for floor heating applications, where the slow thermal response benefits from longer periods (30 minutes) and minimum times (10 minutes). This prevents unnecessary valve cycling and allows the floor to heat and cool gradually.

- **Period Selection**: Choose a period based on your system's thermal mass:
  - Fast systems (small heaters): 5-10 seconds
  - Slow systems (large radiators): 30-60 seconds
  - Floor heating: 20-60 minutes (use defaults or longer)
  
- **Minimum Times**: Set these based on your hardware limitations:
  - Mechanical relays: 1-2 seconds minimum
  - SSRs: Can be much shorter (0.1-0.5 seconds)
  - Heating elements: Consider thermal cycling limits
  - Floor heating valves: 5-10 minutes minimum to prevent excessive wear

## License

MIT