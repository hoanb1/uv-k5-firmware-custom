import scipy.io.wavfile as wav
import numpy as np
import sys

fs, data = wav.read('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/rs41pre_20150802.wav')
data = data.astype(float) - 128.0

# DPLL to extract bits
# 48000 Hz / 4800 baud = 10 samples per bit
samples_per_bit = 10
bits = []

last_sign = data[0] > 0
phase = 0

for i in range(1, len(data)):
    sign = data[i] > 0
    if sign != last_sign:
        last_sign = sign
        phase = 0 # hard sync
    
    phase += 1
    if phase == samples_per_bit // 2:
        # Sample at the center of the bit
        bits.append(1 if sign else 0)
    if phase >= samples_per_bit:
        phase = 0

print(f"Extracted {len(bits)} bits.")
bits_bytes = bytearray(bits)
with open('bits.bin', 'wb') as f:
    f.write(bits_bytes)
