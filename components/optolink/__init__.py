"""Viessmann boiler on the Optolink, P300 protocol.

The hub owns the serial line; every datapoint is an entity of platform optolink
(sensor, text_sensor, number, select, switch) with the address of its value:

  address: 0x5525        # big endian, as in the openv/vitalk tables
  type: s16              # u8 (default), s8, u16, s16, u32, s32; text_sensor also: date
  offset: 1              # value at this byte of a longer read (default 0)
  update_interval: 60s   # required: the polling table is read from the YAML
  divisor: 10            # sensor and number only
"""

import esphome.codegen as cg
from esphome.components import binary_sensor, uart
from esphome.components.sensor import new_sensor, sensor_schema  # "sensor" is our platform module here
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_OFFSET,
    CONF_TIMEOUT,
    CONF_TYPE,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_TOTAL_INCREASING,
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "sensor"]

optolink_ns = cg.esphome_ns.namespace("optolink")
Optolink = optolink_ns.class_("Optolink", cg.Component, uart.UARTDevice)
Datapoint = optolink_ns.class_("Datapoint")
Type = optolink_ns.enum("Type")

TYPES = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4, "date": 7}  # name: size
INT_TYPES = [t for t in TYPES if t != "date"]
MAX_DATA = 32  # bytes per read, as in protocol.h

CONF_OPTOLINK_ID = "optolink_id"
CONF_CONNECTED = "connected"
CONF_ERRORS = "errors"  # failed or refused transactions since boot
CONF_DIVISOR = "divisor"
CONF_OPTIONS = "options"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Optolink),
            cv.Optional(CONF_TIMEOUT, default="2s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ),
            cv.Optional(CONF_ERRORS): sensor_schema(
                accuracy_decimals=0,
                state_class=STATE_CLASS_TOTAL_INCREASING,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                icon="mdi:alert-circle-outline",
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "optolink", baud_rate=4800, data_bits=8, parity="EVEN", stop_bits=2, require_tx=True, require_rx=True
)

DIVISOR = {cv.Optional(CONF_DIVISOR, default=1.0): cv.float_range(min=0, min_included=False)}
OPTIONS = cv.Schema({cv.int_: cv.string_strict})  # raw value: text


def _fits(config):
    if config[CONF_OFFSET] + TYPES[config[CONF_TYPE]] > MAX_DATA:
        raise cv.Invalid(f"offset + size of the value must be at most {MAX_DATA} bytes")
    return config


def datapoint_schema(schema, types=INT_TYPES):
    """The entity schema plus the datapoint options."""
    return cv.All(
        schema.extend(
            {
                cv.GenerateID(CONF_OPTOLINK_ID): cv.use_id(Optolink),
                cv.Required(CONF_ADDRESS): cv.hex_uint16_t,
                cv.Optional(CONF_TYPE, default="u8"): cv.one_of(*types, lower=True),
                cv.Optional(CONF_OFFSET, default=0): cv.int_range(min=0, max=MAX_DATA - 1),
                cv.Required(CONF_UPDATE_INTERVAL): cv.positive_time_period_milliseconds,
            }
        ),
        _fits,
    )


async def register_datapoint(var, config):
    parent = await cg.get_variable(config[CONF_OPTOLINK_ID])
    kind = getattr(Type, config[CONF_TYPE].upper())
    cg.add(var.configure(parent, config[CONF_ADDRESS], kind, config[CONF_OFFSET], config[CONF_UPDATE_INTERVAL]))


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))
    if CONF_CONNECTED in config:
        cg.add(var.set_connected_sensor(await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])))
    if CONF_ERRORS in config:
        cg.add(var.set_errors_sensor(await new_sensor(config[CONF_ERRORS])))
