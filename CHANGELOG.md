# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-05-20

### Added
- **Radiosonde Position Persistence**:
  - The last decoded Radiosonde (RS41) position, altitude, satellite count, and battery voltage are now saved to the EEPROM at address range `0x1F70 - 0x1F7F` upon exit or telemetry update.
  - Telemetry parameters are loaded automatically when opening the Radiosonde app, ensuring tracking data is never lost even if the radio is powered down or rebooted.
- **SI4732 Preset Frequency Channels**:
  - Added 20 quick-access preset slots to save and load SI4732 radio stations (frequency and modulation mode: FM, AM, SSB LSB, USB).
  - Implemented an optimized 3-byte layout per preset slot to minimize EEPROM usage and prevent conflicts with system settings or MDC1200 contact storage.
  - Preset data is saved in a dedicated, safe EEPROM address range: `0x1900 - 0x193B`.
  - Added clear physical feedback: the green status LED glows when the preset loading or saving interface is active.

### Changed
- **SI4732 Preset Interface & Shortcuts**:
  - Replaced the simple key-overlay modes with a dedicated, scrollable full-screen **Preset UI** displaying the 20 preset slots, showing frequency, mode, and whether each slot is empty.
  - Short-press `*` enters **SAVE PRESET** screen (long-press `*` action is removed). F+0 is used to enter **Radio List** screen.
  - Supports browsing (duyệt) presets using the `<` and `>` (`KEY_UP` / `KEY_DOWN`) keys, confirming/triggering the load or save action with the `MENU` key, and canceling/returning to the main screen using the `EXIT` or `*` keys.
  - Quick-jumping to a specific preset slot by pressing number keys `1`-`9` to focus the selection.
