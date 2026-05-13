import re

with open('app/qrcodegen.c', 'r') as f:
    code = f.read()

# Replace the automatic mask selection loop
code = code.replace('''\
	if (mask == qrcodegen_Mask_AUTO) {  // Automatically choose best mask
		int minPenalty = 32767;
		for (int i = 0; i < 8; i++) {
			enum qrcodegen_Mask msk = (enum qrcodegen_Mask)i;
			drawFormatBits(msk, qrcode);
			applyMask(qrcode, qrcode, msk);
			int penalty = getPenaltyScore(qrcode);
			if (penalty < minPenalty) {
				mask = msk;
				minPenalty = penalty;
			}
			applyMask(qrcode, qrcode, msk);  // Undoes the mask due to XOR
		}
	}''', 'mask = qrcodegen_Mask_0;')

with open('app/qrcodegen.c', 'w') as f:
    f.write(code)
