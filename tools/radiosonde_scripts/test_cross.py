import wave
import struct

def find_zero_crossings(wav_file):
    w = wave.open(wav_file, 'rb')
    sr = w.getframerate()
    samples = w.readframes(w.getnframes())
    samples = struct.unpack(f"<{len(samples)//2}h", samples)
    
    avg = sum(samples) / len(samples)
    
    crossings = []
    last_sign = 1 if (samples[0]-avg) >= 0 else 0
    
    for i in range(1, min(len(samples), 500000)):
        sign = 1 if (samples[i]-avg) >= 0 else 0
        if sign != last_sign:
            crossings.append(i)
            last_sign = sign
            
    # Calculate distances between crossings
    dists = [crossings[i+1] - crossings[i] for i in range(len(crossings)-1)]
    
    # Let's count how many crossings have distance ~10 (which is 1 bit at 4800 baud)
    # or ~20 (2 bits), ~30 (3 bits)
    counts = {}
    for d in dists:
        if d not in counts:
            counts[d] = 0
        counts[d] += 1
        
    for k in sorted(counts.keys()):
        if counts[k] > 10:
            print(f"Dist {k}: {counts[k]} times")
            
find_zero_crossings('/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/20140717_402MHz.wav')
