#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

static uint8_t gf_mul(uint8_t x, uint8_t y) {
    uint8_t z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z & 0x80) ? 0x11D : 0);
        if (y & (1 << i)) z ^= x;
    }
    return z;
}

int main() {
    uint8_t data[54] = {0};
    const char* text = "geo:21.0285,105.8541";
    int len = strlen(text);
    if (len > 42) len = 42;

    int bit_pos = 0;
    for (int i = 3; i >= 0; i--) {
        if ((4 >> i) & 1) data[bit_pos / 8] |= (1 << (7 - (bit_pos % 8)));
        bit_pos++;
    }
    for (int i = 7; i >= 0; i--) {
        if ((len >> i) & 1) data[bit_pos / 8] |= (1 << (7 - (bit_pos % 8)));
        bit_pos++;
    }
    for (int i = 0; i < len; i++) {
        for (int j = 7; j >= 0; j--) {
            if ((text[i] >> j) & 1) data[bit_pos / 8] |= (1 << (7 - (bit_pos % 8)));
            bit_pos++;
        }
    }
    bit_pos += 4;
    
    int pad[] = {0xEC, 0x11};
    int pad_idx = 0;
    while ((bit_pos + 7) / 8 <= 44) {
        if (bit_pos % 8 == 0 && bit_pos / 8 < 44) {
            data[bit_pos / 8] = pad[pad_idx ^= 1];
        }
        bit_pos += 8;
    }

    const uint8_t gen[10] = {216, 194, 159, 111, 199, 94, 95, 113, 157, 193};
    uint8_t ec[10] = {0};
    for (int i = 0; i < 44; i++) {
        uint8_t factor = data[i] ^ ec[0];
        for (int j = 0; j < 9; j++) ec[j] = ec[j+1] ^ gf_mul(factor, gen[j]);
        ec[9] = gf_mul(factor, gen[9]);
    }
    for (int i = 0; i < 10; i++) data[44 + i] = ec[i];

    for(int i=0; i<54; i++) printf("%02X ", data[i]);
    printf("\n");
    return 0;
}
