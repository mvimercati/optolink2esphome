import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import CONF_OPTIONS, OPTIONS, TYPES, Datapoint, datapoint_schema, optolink_ns, register_datapoint

OptolinkTextSensor = optolink_ns.class_("OptolinkTextSensor", text_sensor.TextSensor, Datapoint)

CONFIG_SCHEMA = datapoint_schema(
    text_sensor.text_sensor_schema(OptolinkTextSensor).extend({cv.Optional(CONF_OPTIONS, default={}): OPTIONS}),
    types=list(TYPES),
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await register_datapoint(var, config)
    for value, text in config[CONF_OPTIONS].items():
        cg.add(var.add_option(value, text))
