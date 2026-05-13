import wave
import struct

def test_ideal(wav_file):
    w = wave.open(wav_file, 'rb')
    sr = w.getframerate()
    samples = w.readframes(w.getnframes())
    samples = struct.unpack(f"<{len(samples)//2}h", samples)
    
    # 1. Calculate ideal threshold
    avg = sum(samples) / len(samples)
    centered = [s - avg for s in samples]
    
    # 2. Find zero crossings to synchronize
    bits = []
    
    # Just run a simple correlation
    expected = 0x10B6CA11229612F8
    expected_bits = [(expected >> (63 - i)) & 1 for i in range(64)]
    
    best_err = 64
    best_val = 0
    best_offset = 0
    
    # Convert samples to bits (10 samples per bit)
    for offset in range(10):
        # try 10 different starting points (one full bit)
        test_bits = []
        for i in range((len(centered) - offset) // 10 - 1):
            idx = offset + i * 10 + 5 # sample in middle
            if idx >= len(centered):
                break
            test_bits.append(1 if centered[idx] >= 0 else 0)
            
        # Correlate
        for i in range(len(test_bits) - 64):
            window = test_bits[i:i+64]
            err = sum(1 for a, b in zip(window, expected_bits) if a != b)
            
            # check inverted
            err_inv = sum(1 for a, b in zip(window, expected_bits) if a == b)
            
            if err < best_err:
                best_err = err
                val = 0
                for b in window:
                    val = (val << 1) | b
                best_val = val
                best_offset = offset
            
            if err_inv < best_err:
                best_err = err_inv
                val = 0
                for b in window:
                    val = (val << 1) | b
                best_val = val
                best_offset = offset

    print(f"BEST MATCH: offset {best_offset}, err = {best_err}")
    print(f"Hex: {best_val:016X}")

test_ideal('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/rs41pre_20150802.wav')
