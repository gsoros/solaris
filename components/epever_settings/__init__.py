import esphome.codegen as cg
from esphome.components import modbus_controller, number
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["modbus_controller"]
AUTO_LOAD = ["number"]

epever_settings_ns = cg.esphome_ns.namespace("epever_settings")
EpeverSettingsComponent = epever_settings_ns.class_(
    "EpeverSettingsComponent", cg.Component
)
EpeverSettingNumber = epever_settings_ns.class_(
    "EpeverSettingNumber", number.Number, cg.Component
)

CONF_MODBUS_CONTROLLER_ID = "modbus_controller_id"

# Maps config key → (reg_index, friendly description)
VOLTAGE_SETTINGS = {
    "high_voltage_disconnect": (3, "High Voltage Disconnect"),
    "charging_limit_voltage": (4, "Charging Limit Voltage"),
    "over_voltage_reconnect": (5, "Over Voltage Reconnect"),
    "equalization_voltage": (6, "Equalization Voltage"),
    "boost_voltage": (7, "Boost Voltage"),
    "float_voltage": (8, "Float Voltage"),
    "boost_reconnect_voltage": (9, "Boost Reconnect Voltage"),
    "low_voltage_reconnect": (10, "Low Voltage Reconnect"),
    "under_voltage_recover": (11, "Under Voltage Recover"),
    "under_voltage_warning": (12, "Under Voltage Warning"),
    "low_voltage_disconnect": (13, "Low Voltage Disconnect"),
    "discharging_limit_voltage": (14, "Discharging Limit Voltage"),
}


def make_number_schema():
    return number.number_schema(EpeverSettingNumber).extend(
        {
            cv.Optional("min_value", default=10.0): cv.float_,
            cv.Optional("max_value", default=32.0): cv.float_,
            cv.Optional("step", default=0.01): cv.float_,
        }
    )


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EpeverSettingsComponent),
        cv.Required(CONF_MODBUS_CONTROLLER_ID): cv.use_id(
            modbus_controller.ModbusController
        ),
        **{cv.Optional(key): make_number_schema() for key in VOLTAGE_SETTINGS},
    }
)


async def to_code(config):
    mgr = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(mgr, config)

    ctrl = await cg.get_variable(config[CONF_MODBUS_CONTROLLER_ID])
    cg.add(mgr.set_modbus_controller(ctrl))

    for key, (reg_index, description) in VOLTAGE_SETTINGS.items():
        if key not in config:
            continue
        conf = config[key]
        num = await number.new_number(
            conf,
            min_value=conf["min_value"],
            max_value=conf["max_value"],
            step=conf["step"],
        )
        cg.add(num.set_manager(mgr))
        cg.add(num.set_reg_index(reg_index))
        # Register with manager so it can publish_state() on cache ready
        cg.add(mgr.register_number(reg_index, num))
