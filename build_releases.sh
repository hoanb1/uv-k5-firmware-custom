#!/bin/bash
set -e

OUT_DIR="compiled-firmware"

echo "=========================================="
echo "    BUILDING ALL FIRMWARE VARIATIONS"
echo "=========================================="

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

# 1. Bản gốc (Original Features)
echo "------------------------------------------"
echo "[1/5] Building ORIGINAL firmware..."
make clean
make build ENABLE_4732=0 ENABLE_RS41=0
mv firmware.bin "$OUT_DIR/firmware_original.bin"
mv UVK5_MOD.bin "$OUT_DIR/UVK5_MOD_original.bin"

# 2. Bản SI4732 (SI4732 + SSB)
echo "------------------------------------------"
echo "[2/5] Building SI4732 + SSB firmware..."
make clean
make build ENABLE_4732=1 ENABLE_4732SSB=1 ENABLE_RS41=0
mv firmware.bin "$OUT_DIR/firmware_si4732_ssb.bin"
mv UVK5_MODS.bin "$OUT_DIR/UVK5_MODS_si4732_ssb.bin"

# 3. Bản RS41 (Original Features + RS41)
# Note: RS41 enabled will automatically turn off VOX/ALARM/FLASHLIGHT/UART in Makefile to save space
echo "------------------------------------------"
echo "[3/5] Building RS41 firmware..."
make clean
make build ENABLE_4732=0 ENABLE_RS41=1
mv firmware.bin "$OUT_DIR/firmware_rs41.bin"
mv UVK5_MOD.bin "$OUT_DIR/UVK5_MOD_rs41.bin"

# 4. Bản SI4732 + RS41
echo "------------------------------------------"
echo "[4/5] Building SI4732 + SSB + RS41 firmware..."
make clean
make build ENABLE_4732=1 ENABLE_4732SSB=1 ENABLE_RS41=1
mv firmware.bin "$OUT_DIR/firmware_si4732_rs41.bin"
mv UVK5_MODS.bin "$OUT_DIR/UVK5_MODS_si4732_rs41.bin"

# 5. Bản Addons (Spectrum + Doppler + SMS)
echo "------------------------------------------"
echo "[5/5] Building ADDONS (Spectrum + Doppler + SMS) firmware..."
make clean
make build ENABLE_4732=0 ENABLE_RS41=0 ENABLE_SPECTRUM=1 ENABLE_DOPPLER=1 ENABLE_MESSENGER=1 ENABLE_MDC1200=0
mv firmware.bin "$OUT_DIR/firmware_addons.bin"
mv UVK5_MOD.bin "$OUT_DIR/UVK5_MOD_addons.bin"

echo "------------------------------------------"
echo "Generating build information..."
cat << EOF > "$OUT_DIR/info.txt"
===================================================
      QUANSHENG UV-K5 CUSTOM FIRMWARE BUILDS
===================================================
Author: CUSTOM
Date: $(date "+%Y-%m-%d %H:%M:%S")

This directory contains 5 different firmware variations:

1. firmware_original.bin / UVK5_MOD_original.bin
   - Stock features (Flashlight, Alarm, UART, VOX, etc.)

2. firmware_si4732_ssb.bin / UVK5_MODS_si4732_ssb.bin
   - Specialized for radio listening. Enables the SI4732 chip and SSB mode.

3. firmware_rs41.bin / UVK5_MOD_rs41.bin
   - Specialized for weather balloon hunting. Enables the Radiosonde RS41 Decoder.

4. firmware_si4732_rs41.bin / UVK5_MODS_si4732_rs41.bin
   - The All-in-One Build: Includes both the RS41 Decoder and SI4732/SSB radio capabilities.

5. firmware_addons.bin / UVK5_MOD_addons.bin
   - Utility build featuring Spectrum Analyzer, Doppler Satellite Tracking, and SMS Messenger.

* Note: Files starting with UVK5_MOD/UVK5_MODS are packed and scrambled for K5Prog. 
  Files starting with 'firmware' are raw binaries.
EOF

echo "=========================================="
echo "ALL BUILDS COMPLETED SUCCESSFULLY!"
echo "Check the '$OUT_DIR' directory for the output files."
ls -lh "$OUT_DIR"
echo "=========================================="
