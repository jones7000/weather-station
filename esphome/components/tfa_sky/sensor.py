"""TFA Dostmann SKY (30.3195) 433MHz outdoor sensor, received via a CC1101
module. Publishes temperature and humidity to Home Assistant.

The protocol/decoding logic lives in tfa_sky_decoder.cpp, the CC1101 raw
capture in cc1101_receiver.cpp, and tfa_sky.cpp only wires the two together
and reports state - see README.md for the full architecture note.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_FREQUENCY,
    CONF_HUMIDITY,
    CONF_ID,
    CONF_TEMPERATURE,
    CONF_TIMEOUT,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_CELSIUS,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_PERCENT,
)

tfa_sky_ns = cg.esphome_ns.namespace("tfa_sky")
TfaSkyComponent = tfa_sky_ns.class_("TfaSkyComponent", cg.Component)

CONF_SCK_PIN = "sck_pin"
CONF_MISO_PIN = "miso_pin"
CONF_MOSI_PIN = "mosi_pin"
CONF_CSN_PIN = "csn_pin"
CONF_GDO0_PIN = "gdo0_pin"
CONF_RSSI_THRESHOLD = "rssi_threshold"
CONF_SENSOR_ID = "sensor_id"
CONF_SIGNAL_STRENGTH = "signal_strength"
CONF_REINIT_COUNT = "reinit_count"

# Pinned on purpose: the driver is pulled straight from git, and an unpinned
# reference silently picks up upstream rewrites (v3 renamed the whole class)
# on the next clean build.
CC1101_DRIVER_REPO = "https://github.com/LSatan/SmartRC-CC1101-Driver-Lib.git"
CC1101_DRIVER_COMMIT = "de6757ef633b3e626dae5c7835cdb66ab53f9a1d"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TfaSkyComponent),
        # Hardware-SPI pins to the CC1101, defaults match the wiring documented
        # in README.md (Wemos D1 mini).
        cv.Optional(CONF_SCK_PIN, default=14): cv.int_range(min=0, max=16),
        cv.Optional(CONF_MISO_PIN, default=12): cv.int_range(min=0, max=16),
        cv.Optional(CONF_MOSI_PIN, default=13): cv.int_range(min=0, max=16),
        cv.Optional(CONF_CSN_PIN, default=15): cv.int_range(min=0, max=16),
        cv.Optional(CONF_GDO0_PIN, default=5): cv.int_range(min=0, max=16),
        cv.Optional(CONF_FREQUENCY, default=433.92): cv.float_range(min=433.0, max=435.0),
        cv.Optional(CONF_RSSI_THRESHOLD, default=-70): cv.int_range(min=-100, max=0),
        # Optional: only accept frames from one specific sensor ID (useful if
        # multiple TFA SKY sensors are in range). ID is random per power-up,
        # read it from the logs first.
        cv.Optional(CONF_SENSOR_ID): cv.int_range(min=0, max=255),
        # If no valid frame arrives for this long, temperature/humidity are
        # reported as unknown in HA (instead of a stale value) and the CC1101
        # is re-initialised once. 0s disables it.
        cv.Optional(CONF_TIMEOUT, default="15min"): cv.positive_time_period_milliseconds,
        cv.Required(CONF_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_HUMIDITY): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        # RSSI of the received TFA SKY burst (not the ESP's own WiFi signal -
        # that's the separate built-in `wifi_signal` platform). Optional,
        # mainly useful to judge antenna placement/range.
        cv.Optional(CONF_SIGNAL_STRENGTH): sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # How often the CC1101 had to be re-initialised since boot (chip hung,
        # lost its configuration, or reception timed out). Optional, for
        # diagnosing power/wiring problems.
        cv.Optional(CONF_REINIT_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:restart-alert",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(
        var.set_pins(
            config[CONF_SCK_PIN],
            config[CONF_MISO_PIN],
            config[CONF_MOSI_PIN],
            config[CONF_CSN_PIN],
            config[CONF_GDO0_PIN],
        )
    )
    cg.add(var.set_frequency(config[CONF_FREQUENCY]))
    cg.add(var.set_rssi_threshold(config[CONF_RSSI_THRESHOLD]))
    if CONF_SENSOR_ID in config:
        cg.add(var.set_sensor_id(config[CONF_SENSOR_ID]))
    cg.add(var.set_timeout(config[CONF_TIMEOUT]))

    temperature_sensor = await sensor.new_sensor(config[CONF_TEMPERATURE])
    cg.add(var.set_temperature_sensor(temperature_sensor))

    humidity_sensor = await sensor.new_sensor(config[CONF_HUMIDITY])
    cg.add(var.set_humidity_sensor(humidity_sensor))

    if CONF_SIGNAL_STRENGTH in config:
        signal_strength_sensor = await sensor.new_sensor(config[CONF_SIGNAL_STRENGTH])
        cg.add(var.set_signal_strength_sensor(signal_strength_sensor))

    if CONF_REINIT_COUNT in config:
        reinit_count_sensor = await sensor.new_sensor(config[CONF_REINIT_COUNT])
        cg.add(var.set_reinit_count_sensor(reinit_count_sensor))

    # Pulls in the same CC1101 driver used by the plain PlatformIO sketches
    # in src/ and tools/. ESPHome's PlatformIO build runs with the library
    # dependency finder off, so the driver's own SPI.h include isn't picked
    # up automatically - it has to be declared explicitly too.
    cg.add_library("SPI", None)
    cg.add_library(
        "SmartRC-CC1101-Driver-Lib",
        None,
        f"{CC1101_DRIVER_REPO}#{CC1101_DRIVER_COMMIT}",
    )
