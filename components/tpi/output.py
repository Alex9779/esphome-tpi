import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import output, binary_sensor, time as time_, datetime
from esphome.const import CONF_ID, CONF_OUTPUT
from esphome import automation
from esphome.core import ID

CODEOWNERS = ["@alex9779"]
DEPENDENCIES = []

tpi_ns = cg.esphome_ns.namespace("tpi")
TPIOutput = tpi_ns.class_("TPIOutput", output.FloatOutput, cg.Component)

CONF_PERIOD = "period"
CONF_MIN_ON_TIME = "min_on_time"
CONF_MIN_OFF_TIME = "min_off_time"
CONF_NIGHT_OFF_SENSOR = "night_off_sensor"
CONF_NIGHT_OFF_START = "night_off_start"
CONF_NIGHT_OFF_END = "night_off_end"
CONF_TIME_ID = "time_id"

def validate_night_off(config):
    if CONF_NIGHT_OFF_SENSOR in config and (CONF_NIGHT_OFF_START in config or CONF_NIGHT_OFF_END in config):
        raise cv.Invalid("Cannot specify both night_off_sensor and night_off_start/night_off_end")
    if (CONF_NIGHT_OFF_START in config) != (CONF_NIGHT_OFF_END in config):
        raise cv.Invalid("Must specify both night_off_start and night_off_end or neither")
    if CONF_NIGHT_OFF_START in config and CONF_TIME_ID not in config:
        # Require time_id for both datetime components and hardcoded times
        raise cv.Invalid(f"time_id is required when using night_off_start/night_off_end")
    return config

CONFIG_SCHEMA = cv.All(
    output.FLOAT_OUTPUT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(TPIOutput),
            cv.Required(CONF_OUTPUT): cv.use_id(output.BinaryOutput),
            cv.Optional(CONF_PERIOD, default="30min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MIN_ON_TIME, default="10min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MIN_OFF_TIME, default="10min"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_NIGHT_OFF_SENSOR): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_NIGHT_OFF_START): cv.Any(cv.time_of_day, cv.use_id(datetime.TimeEntity)),
            cv.Optional(CONF_NIGHT_OFF_END): cv.Any(cv.time_of_day, cv.use_id(datetime.TimeEntity)),
            cv.Optional(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_night_off,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await output.register_output(var, config)

    binary_output = await cg.get_variable(config[CONF_OUTPUT])
    cg.add(var.set_binary_output(binary_output))
    cg.add(var.set_period(config[CONF_PERIOD]))
    cg.add(var.set_min_on_time(config[CONF_MIN_ON_TIME]))
    cg.add(var.set_min_off_time(config[CONF_MIN_OFF_TIME]))
    
    if CONF_NIGHT_OFF_SENSOR in config:
        sensor = await cg.get_variable(config[CONF_NIGHT_OFF_SENSOR])
        cg.add(var.set_night_off_sensor(sensor))
    
    if CONF_NIGHT_OFF_START in config:
        start = config[CONF_NIGHT_OFF_START]
        end = config[CONF_NIGHT_OFF_END]
        
        # Always set time component for datetime-based night off
        if CONF_TIME_ID in config:
            time_id = await cg.get_variable(config[CONF_TIME_ID])
            cg.add(var.set_time(time_id))
        
        # Check if start/end are datetime component IDs or time strings
        if isinstance(start, ID):
            # Using datetime components - enable USE_DATETIME
            cg.add_define("USE_DATETIME")
            start_dt = await cg.get_variable(start)
            end_dt = await cg.get_variable(end)
            cg.add(var.set_night_off_datetime_start(start_dt))
            cg.add(var.set_night_off_datetime_end(end_dt))
        else:
            # Using hardcoded time strings
            cg.add(var.set_night_off_start(start['hour'], start['minute'], start['second']))
            cg.add(var.set_night_off_end(end['hour'], end['minute'], end['second']))
