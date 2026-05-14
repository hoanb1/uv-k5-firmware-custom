# Quansheng UV-K5 Custom Firmware

This is a highly customized firmware for the Quansheng UV-K5 (and compatible radios), heavily focused on advanced RF functionalities such as **Radiosonde RS41 Decoding**, **SI4732 SSB Reception**, **Spectrum Analysis**, and more. 

All Chinese languages, characters, and fonts have been completely eradicated from this codebase to ensure a **100% English interface**, maximizing the available flash memory for core features.

## ⚠️ 64KB Flash Memory Limitation

The microcontroller (MCU) inside the UV-K5 only has **64KB of Flash memory**. Due to this physical limitation, **it is impossible to enable all features at the same time**. 

To work around this, you must compile specific features by enabling/disabling them in the `Makefile`. If you want to use the Radiosonde RS41 decoder, other non-essential features (like VOX, Alarm, Flashlight, or SMS) must be disabled.

## 🚀 Build Automation (`build_releases.sh`)

To make compiling easier, we have provided an automated build script: `build_releases.sh`.
By running this script, the compiler will automatically build **5 different firmware variations**, each tailored for a specific use case, and save them as independent `.bin` files:

1. **`firmware_original.bin` (~45 KB)**
   - Stock features (Flashlight, Alarm, UART, VOX, etc.) restored.
2. **`firmware_si4732_ssb.bin` (~53 KB)**
   - Specialized for radio listening. Enables the SI4732 sound chip and SSB mode.
3. **`firmware_rs41.bin` (~52 KB)**
   - Specialized for weather balloon hunting. Enables the **Radiosonde RS41 Decoder** (automatically disables Flashlight/Alarm/VOX to save space).
4. **`firmware_si4732_rs41.bin` (~60 KB)**
   - **The All-in-One Build:** Includes both the RS41 Decoder and SI4732/SSB radio capabilities. This build is right at the 60KB limit!
5. **`firmware_addons.bin` (~56 KB)**
   - Utility build featuring **Spectrum Analyzer**, **Doppler Satellite Tracking**, and **SMS Messenger**.

To generate these files, simply run:
```bash
./build_releases.sh
```

## 🛠️ Flashing the Firmware

Because this project is designed for single-boot setups, you simply flash the specific `.bin` file you need for the day.
You can use `k5prog` to flash the firmware directly via the USB-C programming cable:

```bash
./k5prog_src/k5prog -p /dev/ttyUSB0 -F -YYY -b firmware_rs41.bin
```
*(Replace `/dev/ttyUSB0` with your actual serial port).*

## 📖 Feature Details

### Radiosonde RS41 Decoder
[Detailed Hardware Modification & Usage Guide](doc/RADIOSONDE.md)
Requires a hardware mod (tapping the discriminator output to PA8). Decodes live telemetry, GPS coordinates, and generates a dynamic QR Code for immediate smartphone map integration.

### Operating Instructions / Shortcuts

| Key | Function |
|---|---|
| **`UP` / `DOWN`** | Adjust frequency (step size configurable). |
| **`0` - `9`** | Quickly input frequency in frequency mode. |
| **Long Press `M`** | Switch modulation mode (FM/AM/USB). |
| **Long Press `*`** | Scan mode. |
| **Long Press `0` / `F+0`** | Open/Close SI4732 Radio receiver. |
| **Long Press `1` / `F+1`** | Copy current channel to another VFO. |
| **Long Press `2` / `F+2`** | Switch between A/B channels. |
| **Long Press `3` / `F+3`** | Switch between Frequency (VFO) / Channel (MR) mode. |
| **Long Press `6` / `F+6`** | Switch transmit power. |
| **Long Press `8` / `F+8`** | Reverse frequency. |
| **Long Press `9` / `F+9`** | **Open RS41 Radiosonde Decoder** (if compiled). |
| **`F+5`** | Spectrum Analyzer (if compiled). |
| **`F+M`** | Open SMS Messenger (if compiled). |
| **`F+UP`** | Toggle key tone. |
| **`F+DOWN`** | Automatic Doppler shift (if compiled). |
| **`F+EXIT`** | Invert menu navigation (Up/Down). |

## ⚙️ Makefile Compilation Options

If you wish to compile your own custom variations manually, you can pass flags directly to `make`. 
*(Note: `ENABLE_ENGLISH=1` is enabled by default in the Makefile).*

```bash
make clean && make build ENABLE_RS41=1 ENABLE_4732=1 ENABLE_4732SSB=1
```

**Common Flags:**
- `ENABLE_RS41=1` : Radiosonde Decoder
- `ENABLE_4732=1` : SI4732 Radio Support
- `ENABLE_4732SSB=1` : SSB Support
- `ENABLE_SPECTRUM=1` : Spectrum Analyzer
- `ENABLE_DOPPLER=1` : Satellite Doppler Tracking
- `ENABLE_MESSENGER=1` : SMS capabilities
- `ENABLE_UART=1` : Enable PC programming (Chirp/K5Prog)

## Disclaimer

Flashing custom firmware carries inherent risks. You are responsible for ensuring your radio operates within the legal limits of your local regulations.
