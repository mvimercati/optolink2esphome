import esphome.codegen as cg
from esphome.components import sensor

from . import CONF_DIVISOR, DIVISOR, Datapoint, datapoint_schema, optolink_ns, register_datapoint

OptolinkSensor = optolink_ns.class_("OptolinkSensor", sensor.Sensor, Datapoint)

CONFIG_SCHEMA = datapoint_schema(sensor.sensor_schema(OptolinkSensor).extend(DIVISOR))


async def to_code(config):
    var = await sensor.new_sensor(config)
    await register_datapoint(var, config)
    cg.add(var.set_divisor(config[CONF_DIVISOR]))
