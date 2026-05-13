import wave
import struct

def search_wav(wav_file):
    w = wave.open(wav_file, 'rb')
    sr = w.getframerate()
    samples = w.readframes(w.getnframes())
    samples = struct.unpack(f"<{len(samples)//2}h", samples)
    
    avg = sum(samples) / len(samples)
    
    # Just threshold all samples to 1 and 0 to see the raw wave
    raw = [1 if (s-avg) >= 0 else 0 for s in samples]
    
    # Decimate to bitstream (10 samples per bit)
    # We will try all 10 possible offsets
    for offset in range(10):
        bits = []
        for i in range(offset, len(raw), 10):
            bits.append(raw[i])
            
        bit_str = "".join(str(b) for b in bits)
        
        # Now search for the patterns
        # 0x086D53884469481F in binary:
        p1 = f"{0x086D53884469481F:064b}"
        p1_inv = "".join('1' if c=='0' else '0' for c in p1)
        
        # 0x10B6CA11229612F8 in binary:
        p2 = f"{0x10B6CA11229612F8:064b}"
        p2_inv = "".join('1' if c=='0' else '0' for c in p2)
        
        if p1 in bit_str:
            print(f"FOUND 086D... at offset {offset}, bit index {bit_str.find(p1)}")
        if p1_inv in bit_str:
            print(f"FOUND ~086D... at offset {offset}, bit index {bit_str.find(p1_inv)}")
            
        if p2 in bit_str:
            print(f"FOUND 10B6... at offset {offset}, bit index {bit_str.find(p2)}")
        if p2_inv in bit_str:
            print(f"FOUND ~10B6... at offset {offset}, bit index {bit_str.find(p2_inv)}")

search_wav('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/20140717_402MHz.wav')
search_wav('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/rs41pre_20150802.wav')
