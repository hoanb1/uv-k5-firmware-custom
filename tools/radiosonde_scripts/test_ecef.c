#include <stdio.h>
#include <stdint.h>

static uint32_t isqrt64(uint64_t n)
{
    if (n == 0) return 0;
    uint64_t x = n;
    uint64_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return (uint32_t)x;
}

static int32_t iatan2_deg1e6(int32_t y, int32_t x)
{
    if (x == 0 && y == 0) return 0;
    int32_t abs_x = x < 0 ? -x : x;
    int32_t abs_y = y < 0 ? -y : y;
    int32_t angle;
    if (abs_x >= abs_y) {
        int64_t r_1e6 = ((int64_t)abs_y * 1000000LL) / abs_x;
        int64_t r2 = (r_1e6 * r_1e6) / 1000000LL;
        int64_t denom = 1000000LL + (280LL * r2) / 1000LL;
        angle = (int32_t)((r_1e6 * 57295780LL) / (denom));
    } else {
        int64_t r_1e6 = ((int64_t)abs_x * 1000000LL) / abs_y;
        int64_t r2 = (r_1e6 * r_1e6) / 1000000LL;
        int64_t denom = 1000000LL + (280LL * r2) / 1000LL;
        angle = 90000000 - (int32_t)((r_1e6 * 57295780LL) / (denom));
    }
    if (x < 0) angle = 180000000 - angle;
    if (y < 0) angle = -angle;
    return angle;
}

static void rs41_ecef_to_lla(int32_t ecef_x_cm, int32_t ecef_y_cm, int32_t ecef_z_cm,
                              int32_t *lat_1e6, int32_t *lon_1e6, int32_t *alt_cm)
{
    *lon_1e6 = iatan2_deg1e6(ecef_y_cm, ecef_x_cm);
    int32_t x_m = ecef_x_cm / 100;
    int32_t y_m = ecef_y_cm / 100;
    int32_t z_m = ecef_z_cm / 100;
    uint64_t p2 = (int64_t)x_m * x_m + (int64_t)y_m * y_m;
    uint32_t p = isqrt64(p2);
    *lat_1e6 = iatan2_deg1e6(z_m, (int32_t)p);
    uint64_t r2 = p2 + (int64_t)z_m * z_m;
    uint32_t r = isqrt64(r2);
    *alt_cm = ((int32_t)r - 6371000) * 100;
}

int main() {
    // ECEF for approx 21N, 105E (Hanoi) at 10km altitude
    // x = -1540000, y = 5740000, z = 2270000 (roughly in meters)
    int32_t ecef_x_cm = -1540000 * 100;
    int32_t ecef_y_cm = 5740000 * 100;
    int32_t ecef_z_cm = 2270000 * 100;
    int32_t lat, lon, alt;
    rs41_ecef_to_lla(ecef_x_cm, ecef_y_cm, ecef_z_cm, &lat, &lon, &alt);
    printf("Lat: %d, Lon: %d, Alt: %d\n", lat, lon, alt);
    return 0;
}
