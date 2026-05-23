# Quansheng UV-K5 Radiosonde (RS41) Decoder

This document outlines the hardware modifications and software implementations required to decode RS41 weather balloon (Radiosonde) telemetry directly on the Quansheng UV-K5.

## Hardware Modification (Discriminator Tap)

To successfully decode the RS41's 4800 baud GFSK telemetry, the MCU needs access to the raw, unfiltered audio signal (discriminator output) from the BK4819 radio IC. The standard audio path passes through HPF/LPF filters and de-emphasis which distorts the digital waveform.

**Required Mod:**
You must connect **Pin 8 (EARO)** of the BK4819 to **Pin 9 (PA8)** of the DP32G030 MCU via a **DC-blocking capacitor** (e.g., 100nF). This bypasses the internal audio filters and provides a clean waveform to the MCU's ADC for processing.

![Audio Connection Hardware Mod](../images/audio-connection.jpg)
![image](../images/IMG_20260519_073139.jpg)
## Usage Instructions

1. **Start Decoder:** From the main screen, press **`F+9`** to open the RS41 Radiosonde Decoder (Long press `9` is mapped to CW Mode).
2. **Controls:**
   - **`UP` / `DOWN`**: Increase/decrease frequency by 100 kHz (range 400 MHz to 406 MHz).
   - **`MENU` (or `M`)**: Toggle between the Telemetry Monitor screen and the QR Code screen *(if compiled with QR support)*.
   - **`1`**: Quick tune to 400.000 MHz.
   - **`2`**: Quick tune to 402.000 MHz.
   - **`3`**: Quick tune to 403.000 MHz (most common frequency).
   - **`4`**: Quick tune to 404.000 MHz.
   - **`5`**: Quick tune to 405.000 MHz.
   - **`EXIT`**: Exit the Radiosonde mode and return to the main screen.

## Software Features

The firmware utilizes a custom zero-crossing DPLL (Digital Phase Locked Loop) running on the ADC samples to synchronize and extract the digital frames.

### Radiosonde Monitor Screen
Once the signal is locked, the radio displays live telemetry from the weather balloon:
- **Lat / Lon**: Latitude and Longitude (Calculated from RS41 ECEF data).
- **Alt**: Altitude in meters.
- **Sat**: Number of GPS satellites tracked by the balloon. *(Note: If Sat=0, the balloon has no GPS lock and the coordinates will be invalid/factory defaults).*
- **Hs / Vs**: Horizontal and Vertical velocity.

![Radiosonde Monitor Screen](../images/radiosonde-monitor.jpg)

### QR Code Fast Export
To make retrieving the balloon easier, you can switch the display to show a dynamic QR Code by pressing key **9** *(Note: QR Code rendering is fully supported and fits in the tight 64KB flash limit using a lightweight miniqr encoder)*. Scanning this code with a smartphone will immediately open your map application (Google Maps, Apple Maps, etc.) with a pin dropped at the balloon's coordinates.

- **Aspect Ratio Correction**: The UV-K5 LCD has rectangular (non-square) pixels. The firmware uses a precise **3x2 integer scaling** algorithm to ensure the QR code renders as a perfect physical square.
- **Quiet Zones**: The QR code is specifically offset (`offset_y = 6`) to clear the physical black bezel of the screen, ensuring standard white quiet zones are maintained for 100% reliable scanning under any lighting condition.


## Specialized High-Feature Build (RS41 + SI4732 + SSB)

If you have the SI4732 hardware mod and want to keep RS41, SSB, and SI4732 support while disabling other non-essential features to save space, use the following command:

```bash
make clean && make build ENABLE_RS41=1 ENABLE_4732=1 ENABLE_4732SSB=1 ENABLE_ENGLISH=1 && ./k5prog_src/k5prog -p /dev/ttyUSB1 -F -YYY -b firmware.bin
```
