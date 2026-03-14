# Solar Automation Setup
## Files

- `input_number.yaml` — HA helper entities for all tunable parameters
- `blueprint_switch_control.yaml` — Blueprint for per-switch cell voltage control
- `automation_epever_stepping.yaml` — Epever boost+float voltage stepping

---

## Step 1: Add input_number helpers

In `configuration.yaml`:
```yaml
input_number: !include input_number.yaml
```
Or paste the contents of `input_number.yaml` directly under `input_number:`.

Restart HA, then set sensible initial values in the UI (Settings → Helpers).
Suggested starting values for a 10S LiFePO4 24V pack:
- EP Vcm Threshold High: 3.45
- EP Vcm Threshold Low: 3.30
- EP Voltage Step: 0.05
- EP Voltage Min: 24.0
- EP Voltage Max: 28.0
- EP Cooldown: 5 (minutes)

---

## Step 2: Install the blueprint

Copy `blueprint_switch_control.yaml` to:
```
~/homeassistant/config/blueprints/automation/solar/blueprint_switch_control.yaml
```
(create the `solar/` folder if needed)

HA will pick it up automatically — no restart needed.

---

## Step 3: Create per-switch input_number helpers

For each switch (SW0, SW1, ...) create THREE helpers in HA UI
(Settings → Helpers → Create Helper → Number):

| Helper entity ID              | Description              | Suggested range |
|-------------------------------|--------------------------|-----------------|
| input_number.sw0_thresh_hi    | SW0 turn-on threshold    | 3.00 – 3.60 V   |
| input_number.sw0_thresh_lo    | SW0 turn-off threshold   | 3.00 – 3.60 V   |
| input_number.sw0_cooldown_minutes | SW0 relay cooldown   | 1 – 60 s        |

Repeat for sw1_, sw2_, etc.

---

## Step 4: Instantiate blueprint automations

Settings → Automations → Create Automation → From Blueprint → "Cell Voltage Switch Control"

Fill in:
- Max Cell Voltage Sensor: `sensor.sc_bms_max_cell_voltage`
- Smart Switch: `switch.sw0` (or whatever HA named it)
- Turn ON threshold: `input_number.sw0_thresh_hi`
- Turn OFF threshold: `input_number.sw0_thresh_lo`
- Cooldown: `input_number.sw0_cooldown_minutes`

Repeat for each switch.

---

## Step 5: Add Epever stepping automation

Paste `automation_epever_stepping.yaml` into HA:
- Settings → Automations → Create Automation → (three dots) → Edit as YAML
- Paste contents, save.

Or add to `automations.yaml` if you manage automations as files.

---

## Notes

- The Epever stepping automation uses `mode: single` + `max_exceeded: silent`
  with a `delay` at the end of each branch. This means: while the cooldown
  delay is running, new triggers are silently dropped. No relay chatter,
  no queued-up cascade of steps.
- Same pattern for switch blueprints — cooldown delay blocks re-triggering
  within the same instance.
- All thresholds survive HA restarts (input_number helpers are persistent).
- To add a new switch: create 3 helpers, instantiate blueprint. That's it.
