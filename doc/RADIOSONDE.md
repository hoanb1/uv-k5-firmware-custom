# Quansheng UV-K5 Radiosonde (RS41) Decoder

This document outlines the hardware modifications and software implementations required to decode RS41 weather balloon (Radiosonde) telemetry directly on the Quansheng UV-K5.

## Hardware Modification (Discriminator Tap)

To successfully decode the RS41's 4800 baud GFSK telemetry, the MCU needs access to the raw, unfiltered audio signal (discriminator output) from the BK4819 radio IC. The standard audio path passes through HPF/LPF filters and de-emphasis which distorts the digital waveform.

**Required Mod:**
You must connect **Pin 6 (EARO)** of the BK4819 to **PA8** of the DP32G030 MCU via a **DC-blocking capacitor** (e.g., 100nF). This bypasses the internal audio filters and provides a clean waveform to the MCU's ADC for processing.

![Audio Connection Hardware Mod](../images/audio-connection.jpg)

## Hướng dẫn sử dụng (Usage Instructions)

1. **Khởi động tính năng:** Từ màn hình chính, nhấn giữ phím **`9`** (hoặc `F+9` tùy máy) để mở bộ giải mã Radiosonde RS41.
2. **Các phím điều khiển:**
   - **`UP` / `DOWN`**: Tăng/giảm tần số đi 100 kHz (dải từ 400 MHz đến 406 MHz).
   - **`MENU` (hoặc `M`)**: Chuyển đổi qua lại giữa màn hình Thông số (Monitor) và màn hình Mã QR (QR Code).
   - **`1`**: Chuyển nhanh tần số về 400.000 MHz.
   - **`2`**: Chuyển nhanh tần số về 402.000 MHz.
   - **`3`**: Chuyển nhanh tần số về 403.000 MHz (tần số phổ biến nhất).
   - **`4`**: Chuyển nhanh tần số về 404.000 MHz.
   - **`5`**: Chuyển nhanh tần số về 405.000 MHz.
   - **`EXIT`**: Thoát khỏi chế độ Radiosonde và trở về màn hình chính.

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
To make retrieving the balloon easier, you can switch the display to show a dynamic QR Code. Scanning this code with a smartphone will immediately open your map application (Google Maps, Apple Maps, etc.) with a pin dropped at the balloon's coordinates.

- **Aspect Ratio Correction**: The UV-K5 LCD has rectangular (non-square) pixels. The firmware uses a precise **3x2 integer scaling** algorithm to ensure the QR code renders as a perfect physical square.
- **Quiet Zones**: The QR code is specifically offset (`offset_y = 6`) to clear the physical black bezel of the screen, ensuring standard white quiet zones are maintained for 100% reliable scanning under any lighting condition.

## Specialized High-Feature Build (RS41 + SI4732 + SSB)

If you have the SI4732 hardware mod and want to keep RS41, SSB, and SI4732 support while disabling other non-essential features to save space, use the following command:

```bash
make clean && make build ENABLE_RS41=1 ENABLE_4732=1 ENABLE_4732SSB=1 ENABLE_MESSENGER=0 ENABLE_MDC1200=0 ENABLE_FMRADIO=0 ENABLE_UART=0 ENABLE_DTMF_CALLING=0 ENABLE_AIRCOPY=0 ENABLE_ALARM=0 ENABLE_TX1750=0 ENABLE_VOX=0 ENABLE_BIG_FREQ=0 ENABLE_NO_CODE_SCAN_TIMEOUT=0 ENABLE_AM_FIX=0 ENABLE_SQUELCH_MORE_SENSITIVE=0 ENABLE_FASTER_CHANNEL_SCAN=0 ENABLE_COPY_CHAN_TO_VFO=0 ENABLE_SCAN_RANGES=0 ENABLE_CUSTOM_SIDEFUNCTIONS=0 ENABLE_SIDEFUNCTIONS_SEND=0 ENABLE_WIDE_RX=0 ENABLE_KEEP_MEM_NAME=0 ENABLE_RSSI_BAR=0 ENABLE_PWRON_PASSWORD=0 ENABLE_FLASHLIGHT=0 ENABLE_UART_RW_BK_REGS=0 ENABLE_BOOT_BEEPS=0 ENABLE_REVERSE_BAT_SYMBOL=0 ENABLE_SHOW_CHARGE_LEVEL=0 ENABLE_SPECTRUM=0 ENABLE_CHINESE_FULL=0 ENABLE_PINYIN=0 && ./k5prog_src/k5prog -p /dev/ttyUSB1 -F -YYY -b firmware.bin
```
