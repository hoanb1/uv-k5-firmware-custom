import wave
import struct

def extract_dpll_bits(wav_file):
    w = wave.open(wav_file, 'rb')
    sr = w.getframerate()
    samples = w.readframes(w.getnframes())
    samples = struct.unpack(f"<{len(samples)//2}h", samples)
    
    samples_per_bit = sr / 4800.0
    phase_inc = int(0x10000 / samples_per_bit)

    phase_acc = 0
    last_rx_dpll = 0
    bit_integrator = 0

    adc_avg_x512 = 0
    bits = []

    for s in samples:
        adc_val = int((s / 32768.0) * 1000 + 2048)
        
        if adc_avg_x512 == 0:
            adc_avg_x512 = adc_val << 9
        adc_avg_x512 = (adc_avg_x512 * 511 + (adc_val << 9)) >> 9
        threshold = adc_avg_x512 >> 9
        
        centered = adc_val - threshold
        rx = 1 if centered >= 0 else 0
        
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
            bits.append(1 if bit_integrator >= 0 else 0)
            bit_integrator = 0
            
    # Find a long sequence of alternating bits to locate the preamble
    bit_str = "".join(str(b) for b in bits)
    for i in range(len(bit_str) - 256):
        preamble = bit_str[i:i+32]
        if preamble == "10101010101010101010101010101010" or preamble == "01010101010101010101010101010101":
            print(f"Preamble found at {i}!")
            print(bit_str[i:i+256])
            
            # Print hex bytes (MSB first of each 8 bits)
            h = ""
            for j in range(0, 256, 8):
                byte_str = bit_str[i+j:i+j+8]
                if len(byte_str) == 8:
                    h += f"{int(byte_str, 2):02X}"
            print("Hex:", h)
            return
            
extract_dpll_bits('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/rs41pre_20150802.wav')
