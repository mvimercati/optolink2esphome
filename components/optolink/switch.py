import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from . import Datapoint, datapoint_schema, optolink_ns, register_datapoint

OptolinkSwitch = optolink_ns.class_("OptolinkSwitch", switch.Switch, Datapoint)

CONF_ON_VALUE = "on_value"  # written when turned on, until another on value is read (kept in flash)
CONF_OFF_VALUE = "off_value"

CONFIG_SCHEMA = datapoint_schema(
    switch.switch_schema(OptolinkSwitch).extend(
        {
            cv.Required(CONF_ON_VALUE): cv.int_,
            cv.Optional(CONF_OFF_VALUE, default=0): cv.int_,
        }
    )
)


async def to_code(config):
    var = await switch.new_switch(config)
    await register_datapoint(var, config)
    cg.add(var.set_values(config[CONF_ON_VALUE], config[CONF_OFF_VALUE]))
