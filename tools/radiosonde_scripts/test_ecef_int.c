#include <stdio.h>
#include <stdint.h>

// integer sqrt
uint32_t isqrt32(uint32_t n) {
    uint32_t x = n;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

uint64_t isqrt64(uint64_t n) {
    uint64_t x = n;
    uint64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

// return degrees * 1000000
int32_t iatan2(int32_t y, int32_t x) {
    // very crude atan2 in int
    // just for testing if we can do something simple
    // Actually we can use CORDIC!
}
