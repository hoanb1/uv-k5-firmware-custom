# Quansheng UV-K5 Radiosonde (RS41) Decoder - Complete Guide

This document provides a comprehensive guide to the hardware modifications, software implementations, screen modes, and parameter configurations for decoding RS41 weather balloon (Radiosonde) telemetry directly on the Quansheng UV-K5 (and compatible radios).

---

## 1. Hardware Modification

### 1.1 Discriminator Tap (Analog Audio Path)
To successfully decode the RS41's 4800 baud GFSK telemetry, the microcontroller (MCU) needs access to the raw, unfiltered discriminator output from the BK4819 radio chip. The default audio path on the UV-K5 passes through bandpass filters (HPF/LPF) and de-emphasis circuitry which distorts the FSK digital shape.

* **Required Mod:** Connect **Pin 8 (EARO)** of the BK4819 to **Pin 9 (PA8)** of the DP32G030 MCU via a **DC-blocking capacitor** (e.g., 100nF). This bypasses the internal filters and feeds a raw FM demodulated analog waveform into the MCU's ADC (Channel 3).

### 1.2 PA8 DC Bias Configuration
Since the analog signal is AC-coupled via the DC-blocking capacitor, the MCU must establish a stable DC offset on PA8 so the ADC can capture both positive and negative signal swings without clipping.
* **External Pull-Up:** The UV-K5 motherboard has a physical **10kΩ pull-up resistor** to 3.3V on the UART RX programming line (connected to PA8).
* **Internal Pull-Down:** The firmware configures the MCU's **internal weak pull-down resistor (~41.5kΩ)** while keeping the internal pull-up disabled.
* **Resulting Bias:** This creates a voltage divider biasing the pin at approximately **~2.66V (ADC DC offset ~3306)**. This stable offset leaves enough headroom for the signal to swing up to $V_{CC}$ (4095) and down to GND (0) without clipping.

---

## 2. Keypad Controls & Parameters

When in Radiosonde Mode, the keyboard is mapped to the following functions:

| Key | Action | Description |
|---|---|---|
| **`UP` / `DOWN`** | Adjust Frequency | Steps frequency up or down based on the selected tuning step. |
| **`STAR (*)`** | Cycle Tuning Step | Cycles between **1M**, **100k**, **10k**, and **1k** Hz frequency steps. |
| **`0` - `6`** | Frequency Presets | Quick-tune presets: **`0`**: 400.0 MHz, **`1`**: 401.0 MHz, **`2`**: 402.0 MHz, **`3`**: 403.0 MHz, **`4`**: 404.0 MHz, **`5`**: 405.0 MHz, **`6`**: 406.0 MHz. |
| **`7`** | Switch to Diagnostic | Shows real-time DSP metrics, ADC levels, error rates, and decoder states. |
| **`8`** | Switch to Monitor | Shows live decoded weather balloon telemetry (lat, lon, speed, battery, etc.). |
| **`9`** | Switch to QR Code | Renders a scan-ready QR code of the current balloon's coordinates. |
| **`MENU`** | Cycle AGC Mode | Cycles RF receiver gain: **Auto** $\rightarrow$ **Low** $\rightarrow$ **Mid** $\rightarrow$ **High** $\rightarrow$ **Max**. |
| **`F`** | Toggle Speaker Audio | Mutes or unmutes the raw GFSK audio sidetone through the speaker. |
| **`SIDE 1`** | HPF Compensation | Toggles the software high-pass filter compensation: **`RAW`** (direct samples) vs **`INT`** (integrator compensation). |
| **`SIDE 2`** | Audio LPF Bandwidth | Cycles the BK4819 post-demodulator low-pass filter (AFTxLPF2): **`WID`** (4.5 kHz) $\rightarrow$ **`MID`** (3.0 kHz) $\rightarrow$ **`NAR`** (2.5 kHz). |
| **`EXIT`** | Exit Application | Safely closes the application, saves coordinates, restores registers, and returns to main UI. |

---

## 3. Telemetry Parameter Adjustments & DSP Tuning

### 3.1 Audio LPF Bandwidth (`SIDE 2`)
Due to the removal of the Reed-Solomon ECC (to fit the code inside the tight 64KB Flash), noise immunity must be maximized in the analog/DSP stage.
* **`WID` (4.5 kHz):** Standard bandwidth. Best for strong signals or when there is frequency drift.
* **`MID` (3.0 kHz):** Filters out high-frequency noise above the 4800 baud FSK fundamental frequency (2.4 kHz). Significantly improves weak signal decoding.
* **`NAR` (2.5 kHz):** Maximum noise filtering. Tightest bandwidth, cuts out almost all high-frequency noise but requires precise frequency centering.

### 3.2 HPF Integration Compensation (`SIDE 1`)
* **`RAW`:** Demodulates based on direct ADC amplitude. Best for strong, stable signals.
* **`INT`:** Uses software integration to compensate for any low-frequency DC drift or high-pass filtering introduced by the DC-blocking capacitor. Improves lock stability on weak signals.

---

## 4. Screen Modes

### 4.1 Telemetry Monitor Screen (`Key 8`)
Displays live decoded parameters from the RS41:
* **ID:** The unique 8-character serial number of the sonde (e.g., `S4930291`). Displays `(live)` when receiving active frames, and `(saved)` when showing restored data from EEPROM.
* **Lat / Lon:** Latitude and Longitude (converted from ECEF coordinates using floating-point emulation).
* **Alt:** Altitude in meters.
* **Vel:** Horizontal (`H`) and Vertical (`V`) velocity in m/s.
* **Sat:** Number of satellites tracked by the sonde.
* **Time:** UTC GPS time.
* **Batt:** Battery voltage of the sonde in Volts.

### 4.2 Diagnostic Screen (`Key 7`)
Used for receiver alignment and signal troubleshooting:
* **RSSI:** Coded signal strength.
* **P2P:** Peak-to-peak amplitude of the signal read by the ADC. The Auto-Gain controller targets **600 to 1000** to maximize sensitivity without causing clipping.
* **DC:** The DC offset of the ADC (should remain stable around **3306** due to the internal pull-down resistor).
* **Min / Err:** `Min` is the best correlation error count (must be $\le 12$ to detect a frame). `Err` shows current correlation errors. Under pure noise, `Min` typically hovers around 13-14.
* **Rx / OK:** Count of total frames received vs frames with valid CRC.
* **Sh / Inv:** Internal DSP debug parameters (bit shift offset and signal inversion status).

### 4.3 QR Code Export Screen (`Key 9`)
Renders a scan-ready QR code of the current balloon's coordinates (`geo:lat,lon`).
* **Aspect Ratio Correction:** The UV-K5 LCD has non-square pixels. The firmware applies a **3x2 integer scaling** to make the QR code a physically perfect square.
* **Quiet Zone Alignment:** The QR code is specifically offset (`offset_y = 6`) to clear the physical black bezel of the screen, ensuring a white quiet zone is maintained for 100% reliable scanning.

---

## 5. Non-volatile Storage (EEPROM)

To ensure your tracking data is never lost (even if the battery dies or the radio reboots), the firmware saves telemetry data to the EEPROM:
* **Trigger:** Saves automatically upon exiting the app, or periodically every 15 seconds if a new valid coordinate is decoded.
* **EEPROM Address:** `0x0E28 - 0x0E3F` (allocated to prevent conflicts with channel configurations).
* **Restored Data:** When you launch the Radiosonde app, it immediately loads the last saved coordinates, altitude, and Sonde ID, displaying them as `(saved pos)` until a live balloon is locked.

---

## 6. Build Instructions

To compile the specialized build supporting RS41 decoding, run the following command:

```bash
make clean && make build ENABLE_RS41=1
```

To flash the compiled `firmware.bin` using `k5prog`:

```bash
./k5prog_src/k5prog -b firmware.bin -YYYYYY -F -p /dev/ttyUSB0
```
