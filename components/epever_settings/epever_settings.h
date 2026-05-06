#pragma once
#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/modbus_controller/modbus_controller.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace epever_settings {

static const char *const TAG = "epever_settings";

// Register map — contiguous block 0x9000–0x900E (15 regs), written atomically
// Index in block:
//  0  BATTERY_TYPE            0x9000
//  1  BATTERY_CAPACITY        0x9001
//  2  BATTERY_TEMP_COEFF      0x9002
//  3  HIGH_VOLTAGE_DISCONNECT 0x9003  (hvd)
//  4  CHARGING_LIMIT_VOLTAGE  0x9004  (cl)
//  5  OVER_VOLTAGE_RECONNECT  0x9005  (ovr)
//  6  EQUALIZATION_VOLTAGE    0x9006  (eqv)
//  7  BOOST_VOLTAGE           0x9007  (bv)
//  8  FLOAT_VOLTAGE           0x9008  (fv)
//  9  BOOST_RECONNECT_VOLTAGE 0x9009  (brv)
// 10  LOW_VOLTAGE_RECONNECT   0x900A  (lvr)
// 11  UNDER_VOLTAGE_RECOVER   0x900B  (uvr)
// 12  UNDER_VOLTAGE_WARNING   0x900C  (uvw)
// 13  LOW_VOLTAGE_DISCONNECT  0x900D  (lvd)
// 14  DISCHARGING_LIMIT_VOLT  0x900E  (dl)
#define EPEVER_SETTINGS_BASE 0x9000
#define EPEVER_SETTINGS_COUNT 15

// Indices
#define IDX_HVD 3   // High Voltage Disconnect
#define IDX_CL 4    // Charging Limit Voltage
#define IDX_OVR 5   // Over Voltage Reconnect
#define IDX_EQV 6   // Equalization Voltage
#define IDX_BV 7    // Boost Voltage
#define IDX_FV 8    // Float Voltage
#define IDX_BRV 9   // Boost Reconnect Voltage
#define IDX_LVR 10  // Low Voltage Reconnect
#define IDX_UVR 11  // Under Voltage Recover
#define IDX_UVW 12  // Under Voltage Warning
#define IDX_LVD 13  // Low Voltage Disconnect
#define IDX_DL 14   // Discharging Limit Voltage

// Forward declaration so EpeverSettingsComponent can hold pointers to number entities
class EpeverSettingNumber;
class EpeverCompositeNumber;

class EpeverSettingsComponent : public Component {
 public:
  void set_modbus_controller(modbus_controller::ModbusController *ctrl) { controller_ = ctrl; }

  // Called from __init__.py codegen to register each number entity by index
  void register_number(uint8_t index, EpeverSettingNumber *num) {
    if (index < EPEVER_SETTINGS_COUNT)
      numbers_[index] = num;
  }

  // Write a value to a specific register index. Values are cached and written to the device in batches
  void write_setting(uint8_t reg_index, float value) {
    if (!is_ready()) {
      ESP_LOGW(TAG,
               "Write ignored — cache not yet populated from Epever "
               "(reg[%d] = %.2f). Retry after first poll cycle.",
               reg_index, value);
      return;
    }
    pending_[reg_index] = (uint16_t) (value * 100.0f);
    dirty_ = true;
    ESP_LOGI(TAG, "Setting reg[%d] queued: %.2f (raw %d)", reg_index, value, pending_[reg_index]);
    this->write_settings_();
  }

  // Method for writing the same value to multiple indexes with one call
  // and save some EEPROM write cycles.
  // @param reg_index Array of register indexes to write to (e.g. {IDX_HVD, IDX_CL, IDX_OVR}).
  // @param value The value to write to all specified indexes.
  // @param count The number of indexes in the reg_index array.
  void write_same_value_to_multiple_indexes(uint8_t reg_index[], uint8_t count, float value) {
    if (!is_ready()) {
      ESP_LOGW(TAG,
               "Write ignored — cache not yet populated from Epever "
               "(reg[%d] = %.2f). Retry after first poll cycle.",
               reg_index[0], value);
      return;
    }
    for (uint8_t i = 0; i < count; i++) {
      pending_[reg_index[i]] = (uint16_t) (value * 100.0f);
      ESP_LOGI(TAG, "Setting reg[%d] queued: %.2f (raw %d)", reg_index[i], value, pending_[reg_index[i]]);
    }
    dirty_ = true;
    this->write_settings_();
  }

  void write_cl_bv_fv_eqv(float value) {
    uint8_t reg_index[] = {IDX_CL, IDX_BV, IDX_FV, IDX_EQV};
    write_same_value_to_multiple_indexes(reg_index, 4, value);
  }

  // Update individual entity states after composite write
  void update_individual_states(float value);

  // Populate cache from a freshly-read register block (called on poll via lambda).
  // Tracks how many distinct registers have been seen; writes are blocked
  // until all EPEVER_SETTINGS_COUNT registers have been read at least once.
  // On the poll that completes the cache, publish initial states to all
  // registered number entities so the HA UI populates immediately.
  void update_cache(uint8_t index, uint16_t raw_value) {
    // ESP_LOGD(TAG, "update_cache() called for index %d", index);
    bool was_ready = is_ready();
    if (!cache_seen_[index]) {
      cache_seen_[index] = true;
      cache_ready_count_++;
    }
    pending_[index] = raw_value;
    if (!was_ready && is_ready()) {
      ESP_LOGI(TAG, "Cache fully populated — writes now enabled. Publishing initial states.");
      publish_initial_states_();
    }
  }

  bool is_ready() const { return cache_ready_count_ == EPEVER_SETTINGS_COUNT; }

 protected:
  modbus_controller::ModbusController *controller_{nullptr};
  EpeverSettingNumber *numbers_[EPEVER_SETTINGS_COUNT]{};
  uint16_t pending_[EPEVER_SETTINGS_COUNT]{};
  bool cache_seen_[EPEVER_SETTINGS_COUNT]{};
  uint8_t cache_ready_count_{0};
  bool dirty_{false};

  // Push current cache values to all registered number entities.
  // Only called once, when the cache first becomes fully populated.
  // Voltage registers (index >= 3) are stored x100 — divide back to float V.
  // Non-voltage registers (0=type, 1=capacity, 2=temp_coeff) are raw integers.
  void publish_initial_states_();  // defined after EpeverSettingNumber

  void enforce_constraints_() {
    uint16_t *r = pending_;

    // Hard-coded range guards
    if (r[IDX_CL] < 1200)
      r[IDX_CL] = 1200;
    if (r[IDX_CL] > 6000)
      r[IDX_CL] = 6000;
    if (r[IDX_DL] < 800)
      r[IDX_DL] = 800;
    if (r[IDX_DL] > 5000)
      r[IDX_DL] = 5000;

    // HVD > CL >= EQV >= BV >= FV > BRV
    if (r[IDX_HVD] <= r[IDX_CL])
      r[IDX_HVD] = r[IDX_CL] + 1;
    if (r[IDX_EQV] > r[IDX_CL])
      r[IDX_EQV] = r[IDX_CL];
    if (r[IDX_BV] > r[IDX_EQV])
      r[IDX_BV] = r[IDX_EQV];
    if (r[IDX_FV] > r[IDX_BV])
      r[IDX_FV] = r[IDX_BV];
    if (r[IDX_BRV] >= r[IDX_FV])
      r[IDX_BRV] = r[IDX_FV] - 1;

    // HVD > OVR
    if (r[IDX_OVR] >= r[IDX_HVD])
      r[IDX_OVR] = r[IDX_HVD] - 1;

    // LVR > LVD >= DL
    if (r[IDX_LVD] < r[IDX_DL])
      r[IDX_LVD] = r[IDX_DL];
    if (r[IDX_LVR] <= r[IDX_LVD])
      r[IDX_LVR] = r[IDX_LVD] + 1;

    // UVR > UVW >= DL
    if (r[IDX_UVW] < r[IDX_DL])
      r[IDX_UVW] = r[IDX_DL] + 1;
    if (r[IDX_UVR] <= r[IDX_UVW])
      r[IDX_UVR] = r[IDX_UVW] + 1;

    // BRV > LVD
    if (r[IDX_BRV] <= r[IDX_LVD])
      r[IDX_BRV] = r[IDX_LVD] + 1;
  }

  void write_settings_() {
    if (!dirty_ || controller_ == nullptr)
      return;

    enforce_constraints_();

    ESP_LOGI(TAG, "Writing settings block 0x9000-0x900E:");
    for (int i = 0; i < EPEVER_SETTINGS_COUNT; i++) {
      ESP_LOGI(TAG, "  [0x%04X] = %d (%.2fV)", EPEVER_SETTINGS_BASE + i, pending_[i], pending_[i] / 100.0f);
    }

    std::vector<uint16_t> data(pending_, pending_ + EPEVER_SETTINGS_COUNT);
    modbus_controller::ModbusCommandItem cmd = modbus_controller::ModbusCommandItem::create_write_multiple_command(
        controller_, EPEVER_SETTINGS_BASE, EPEVER_SETTINGS_COUNT, data);
    controller_->queue_command(cmd);
    dirty_ = false;
    ESP_LOGI(TAG, "Settings block queued for write.");
  }
};

// Number entity that delegates writes to EpeverSettingsComponent
class EpeverSettingNumber : public number::Number, public Component {
 public:
  void set_manager(EpeverSettingsComponent *mgr) { manager_ = mgr; }
  void set_reg_index(uint8_t idx) { reg_index_ = idx; }

 protected:
  EpeverSettingsComponent *manager_{nullptr};
  uint8_t reg_index_{0};

  void control(float value) override {
    this->publish_state(value);
    manager_->write_setting(reg_index_, value);
  }
};

// Composite number entity that writes same value to CL, BV, FV, EQV registers
class EpeverCompositeNumber : public number::Number, public Component {
 public:
  void set_manager(EpeverSettingsComponent *mgr) { manager_ = mgr; }

 protected:
  EpeverSettingsComponent *manager_{nullptr};

  void control(float value) override {
    // Write to device
    manager_->write_cl_bv_fv_eqv(value);
    // Update individual entity states
    manager_->update_individual_states(value);
  }
};

// Defined here (after EpeverSettingNumber) so publish_state() is accessible
inline void EpeverSettingsComponent::publish_initial_states_() {
  for (uint8_t i = 0; i < EPEVER_SETTINGS_COUNT; i++) {
    if (numbers_[i] == nullptr)
      continue;
    // Indices 0-2 are non-voltage (type, capacity, temp_coeff) — raw integer value
    // Indices 3-14 are voltages stored x100 — convert back to float V
    float val = (i >= 3) ? pending_[i] / 100.0f : (float) pending_[i];
    numbers_[i]->publish_state(val);
    ESP_LOGD(TAG, "Published initial state for reg[%d]: %.2f", i, val);
  }
}

// Update individual entity states after composite write
inline void EpeverSettingsComponent::update_individual_states(float value) {
  // Update CL, BV, FV, EQV entities if they exist
  if (numbers_[IDX_CL] != nullptr) {
    numbers_[IDX_CL]->publish_state(value);
    ESP_LOGD(TAG, "Updated CL state: %.2f", value);
  }
  if (numbers_[IDX_BV] != nullptr) {
    numbers_[IDX_BV]->publish_state(value);
    ESP_LOGD(TAG, "Updated BV state: %.2f", value);
  }
  if (numbers_[IDX_FV] != nullptr) {
    numbers_[IDX_FV]->publish_state(value);
    ESP_LOGD(TAG, "Updated FV state: %.2f", value);
  }
  if (numbers_[IDX_EQV] != nullptr) {
    numbers_[IDX_EQV]->publish_state(value);
    ESP_LOGD(TAG, "Updated EQV state: %.2f", value);
  }
}

}  // namespace epever_settings
}  // namespace esphome
