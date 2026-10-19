import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import CONF_MAX_VALUE, CONF_MIN_VALUE, CONF_STEP

from . import CONF_DIVISOR, DIVISOR, Datapoint, datapoint_schema, optolink_ns, register_datapoint

OptolinkNumber = optolink_ns.class_("OptolinkNumber", number.Number, Datapoint)

CONFIG_SCHEMA = datapoint_schema(
    number.number_schema(OptolinkNumber).extend(
        {
            cv.Required(CONF_MIN_VALUE): cv.float_,
            cv.Required(CONF_MAX_VALUE): cv.float_,
            cv.Optional(CONF_STEP, default=1): cv.positive_float,
            **DIVISOR,
        }
    )
)


async def to_code(config):
    var = await number.new_number(
        config, min_value=config[CONF_MIN_VALUE], max_value=config[CONF_MAX_VALUE], step=config[CONF_STEP]
    )
    await register_datapoint(var, config)
    cg.add(var.set_divisor(config[CONF_DIVISOR]))
