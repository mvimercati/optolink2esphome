import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv

from . import CONF_OPTIONS, OPTIONS, Datapoint, datapoint_schema, optolink_ns, register_datapoint

OptolinkSelect = optolink_ns.class_("OptolinkSelect", select.Select, Datapoint)

CONFIG_SCHEMA = datapoint_schema(select.select_schema(OptolinkSelect).extend({cv.Required(CONF_OPTIONS): OPTIONS}))


async def to_code(config):
    options = config[CONF_OPTIONS]  # raw value: option, in the order given
    var = await select.new_select(config, options=list(options.values()))
    await register_datapoint(var, config)
    cg.add(var.set_values(list(options)))
