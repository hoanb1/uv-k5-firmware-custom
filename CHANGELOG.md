# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-05-20

### Added
- **Radiosonde Position Persistence**:
  - The last decoded Radiosonde (RS41) position, altitude, satellite count, and battery voltage are now saved to the EEPROM at address range `0x1F70 - 0x1F7F` upon exit or telemetry update.
  - Telemetry parameters are loaded automatically when opening the Radiosonde app, ensuring tracking data is never lost even if the radio is powered down or rebooted.
- **SI4732 Preset Frequency Channels**:
  - Added 9 quick-access preset slots to save and load SI4732 radio stations (frequency and modulation mode: FM, AM, SSB LSB, USB).
  - Implemented an optimized 3-byte layout per preset slot to minimize EEPROM usage and prevent conflicts with system settings or MDC1200 contact storage.
  - Preset data is saved in a dedicated, safe EEPROM address range: `0x1F50 - 0x1F6B`.
  - Added clear physical feedback: the green status LED glows when the preset loading or saving interface is active.

### Changed
- **SI4732 Shortcuts**:
  - Short-pressing the `*` key opens the **Load Preset** menu (press `1`-`9` to load).
  - Long-pressing the `*` key opens the **Save Preset** menu (press `1`-`9` to save).
  - Pressing `EXIT` or `*` cancels and exits preset mode.
