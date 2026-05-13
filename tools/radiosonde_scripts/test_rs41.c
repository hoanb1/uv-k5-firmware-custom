#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "app/rs41.h"

int main(int argc, char **argv) {
    FILE *f = fopen("bits.bin", "rb");
    if (!f) return 1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *bits = malloc(size);
    fread(bits, 1, size, f);
    fclose(f);

    RS41_Decoder_t dec;
    RS41_Init(&dec);

    int valid_frames = 0;
    for (long i = 0; i < size; i++) {
        RS41_State_t prev_state = dec.state;
        bool ret = RS41_ProcessBit(&dec, bits[i]);
        if (ret && dec.data.valid) {
            valid_frames++;
            printf("Valid frame! Frame %u | ID: %s | Lat: %.5f | Lon: %.5f | Alt: %.1fm\n",
                dec.data.frame_nr, dec.data.sonde_id,
                dec.data.lat_1e6 / 1000000.0,
                dec.data.lon_1e6 / 1000000.0,
                dec.data.alt_cm / 100.0);
        }
    }
    printf("Total valid frames decoded: %d\n", valid_frames);
    return 0;
}
