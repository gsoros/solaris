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
#define IDX_HVD 3
#define IDX_CL 4
#define IDX_OVR 5
#define IDX_EQV 6
#define IDX_BV 7
#define IDX_FV 8
#define IDX_BRV 9
#define IDX_LVR 10
#define IDX_UVR 11
#define IDX_UVW 12
#define IDX_LVD 13
#define IDX_DL 14

// Forward declaration so EpeverSettingsComponent can hold pointers to number entities
class EpeverSettingNumber;

class EpeverSettingsComponent : public Component {
 public:
  void set_modbus_controller(modbus_controller::ModbusController *ctrl) { controller_ = ctrl; }

  // Called from __init__.py codegen to register each number entity by index
  void register_number(uint8_t index, EpeverSettingNumber *num) {
    if (index < EPEVER_SETTINGS_COUNT)
      numbers_[index] = num;
  }

  // Called by number entities when user changes a value in HA
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

    // Range guards from original FW
    if (r[IDX_CL] < 1200)
      r[IDX_CL] = 1200;
    if (r[IDX_CL] > 6000)
      r[IDX_CL] = 6000;
    if (r[IDX_DL] < 800)
      r[IDX_DL] = 800;
    if (r[IDX_DL] > 5000)
      r[IDX_DL] = 5000;

    // hvd > cl >= eqv >= bv >= fv > brv
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

    // hvd > ovr
    if (r[IDX_OVR] >= r[IDX_HVD])
      r[IDX_OVR] = r[IDX_HVD] - 1;

    // lvr > lvd >= dl
    if (r[IDX_LVD] < r[IDX_DL])
      r[IDX_LVD] = r[IDX_DL];
    if (r[IDX_LVR] <= r[IDX_LVD])
      r[IDX_LVR] = r[IDX_LVD] + 1;

    // uvr > uvw >= dl
    if (r[IDX_UVW] < r[IDX_DL])
      r[IDX_UVW] = r[IDX_DL] + 1;
    if (r[IDX_UVR] <= r[IDX_UVW])
      r[IDX_UVR] = r[IDX_UVW] + 1;

    // brv > lvd
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

}  // namespace epever_settings
}  // namespace esphome
