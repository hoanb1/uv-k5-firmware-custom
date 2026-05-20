# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased] - 2026-05-20

### Added
- **CW Keyboard Text Entry & Auto-Transmit**:
  - Added keypad multi-tap character entry using keys `0`-`9` and backspace using the `*` key to compose text messages directly in CW mode.
  - Added automated text-to-CW transmission over the air triggered by the `MENU` key, with visual status (`TX` badge) and LED flashing matching the dits/dahs.
  - Integrated transmission abort functionality via the `EXIT` key.
  - Stripped the optional QR Code display from the Radiosonde decoder to save flash space.
  - Optimized code footprint by removing all division/modulus operations from `app/cw.c` (using lookup tables for WPM dit durations and index boundary checks), saving valuable flash space to fit the combined firmware.
  - Added a speaker audio toggle in CW mode using the `F` key to switch between Sound On (`SON`) and Mute (`MUT`). Enables forced speaker output for listening to CW signals, though tone generation has known limitations on FM-only hardware (currently heard as ticking/clicking).
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
  - Short-press `*` or `MENU` enters **SAVE PRESET** screen (long-press `*` action is removed). F+0 is used to enter **Radio List** screen.
  - Supports browsing (duyệt) presets using the `<` and `>` (`KEY_UP` / `KEY_DOWN`) keys, confirming/triggering the load or save action with the `MENU` key, and canceling/returning to the main screen using the `EXIT` or `*` keys.
  - Quick-jumping to a specific preset slot by pressing number keys `1`-`9` to focus the selection.
