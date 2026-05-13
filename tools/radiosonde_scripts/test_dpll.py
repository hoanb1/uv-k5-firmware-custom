import wave
import struct
import numpy as np

def test_dpll(wav_file):
    w = wave.open(wav_file, 'rb')
    sr = w.getframerate()
    samples = w.readframes(w.getnframes())
    samples = struct.unpack(f"<{len(samples)//2}h", samples)
    
    samples_per_bit = sr / 4800.0
    phase_inc = int(0x10000 / samples_per_bit)

    phase_acc = 0
    last_rx_dpll = 0
    bit_integrator = 0

    shift_reg = 0
    min_errors = 64
    last_shift_hi = 0
    last_shift_lo = 0
    
    rs41_header_64 = 0xF812962211CAB610

    adc_avg_x512 = 0
    
    bit_count = 0

    for i, s in enumerate(samples):
        # Scale to match UV-K5 ADC range roughly (0-4095) for realistic simulation
        adc_val = int((s / 32768.0) * 1000 + 2048)
        
        if adc_avg_x512 == 0:
            adc_avg_x512 = adc_val << 9
        adc_avg_x512 = (adc_avg_x512 * 511 + (adc_val << 9)) >> 9
        threshold = adc_avg_x512 >> 9
        
        centered = adc_val - threshold
        if centered > 50:
            rx = 1
        elif centered < -50:
            rx = 0
        else:
            rx = last_rx_dpll
        
        bit_integrator += centered
        
        phase_acc += phase_inc
        
        if rx != last_rx_dpll:
            last_rx_dpll = rx
            phase_error = phase_acc & 0xFFFF
            if phase_error > 0x8000:
                phase_error -= 0x10000
            phase_acc -= int(phase_error / 8)
            
        if phase_acc >= 0x10000:
            phase_acc -= 0x10000
            bit = 1 if bit_integrator >= 0 else 0
            bit_integrator = 0
            
            shift_reg = (shift_reg >> 1) | (bit << 63)
            bit_count += 1
            
            if bit_count >= 64:
                xor_val = shift_reg ^ rs41_header_64
                errors = bin(xor_val).count('1')
                
                xor_inv = shift_reg ^ (~rs41_header_64 & 0xFFFFFFFFFFFFFFFF)
                errors_inv = bin(xor_inv).count('1')
                
                if errors < min_errors or errors_inv < min_errors:
                    min_errors = min(errors, errors_inv)
                    last_shift_hi = shift_reg >> 32
                    last_shift_lo = shift_reg & 0xFFFFFFFF
                    print(f"Sample {i}: New min errors = {min_errors}, Hex = {last_shift_hi:08X}{last_shift_lo:08X}")
                    
                if min_errors <= 16:
                    print("SYNC FOUND!")
                    return

    print(f"End of file. Best match had {min_errors} errors: {last_shift_hi:08X}{last_shift_lo:08X}")

test_dpll('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/20140717_402MHz.wav')
