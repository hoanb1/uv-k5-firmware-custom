import wave
import struct

def print_bits(wav_file):
    w = wave.open(wav_file, 'rb')
    sr = w.getframerate()
    samples = w.readframes(w.getnframes())
    samples = struct.unpack(f"<{len(samples)//2}h", samples)
    
    avg = sum(samples) / len(samples)
    centered = [s - avg for s in samples]
    
    # Extract bits at perfect baud rate assuming start at 0
    bits = []
    # We will search for a preamble of at least 16 alternating bits to align
    
    # Just threshold all samples to 1 and 0 to see the raw wave
    raw = [1 if c >= 0 else 0 for c in centered]
    
    # find preamble: a long sequence of alternating 1 and 0 (or 10 samples of 1, 10 samples of 0)
    # since 10 samples per bit
    for i in range(100000):
        # check if we have alternating bits
        # sample at i, i+10, i+20...
        b = []
        for j in range(32):
            b.append(raw[i + j*10])
        
        # Check if b is alternating
        alt = True
        for j in range(len(b)-1):
            if b[j] == b[j+1]:
                alt = False
                break
        
        if alt:
            print(f"Preamble found at {i}")
            # Now extract the next 128 bits
            data_bits = []
            for j in range(128):
                idx = i + 32*10 + j*10
                if idx < len(raw):
                    data_bits.append(raw[idx])
            
            # Print as hex assuming bits are LSB of byte (group by 8)
            # MSB first grouping
            hex_msb = ""
            for j in range(0, min(128, len(data_bits)), 8):
                val = 0
                for k in range(8):
                    val = (val << 1) | data_bits[j+k]
                hex_msb += f"{val:02X}"
            
            print("Hex grouped (MSB first):", hex_msb)
            print("Hex inverted:", "".join([f"{~int(c, 16) & 15:X}" for c in hex_msb]))
            return

print_bits('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/20140717_402MHz.wav')
