#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int main() {
    FILE *f = fopen("/home/hoan/DATA/rs41-android/RS-decoder/rs41/wav/rs41pre_20150802.wav", "rb");
    if (!f) return 1;
    // We don't have the descrambled frame from wav directly.
    // Let's use the rs1729 tool to extract a descrambled frame!
    return 0;
}
