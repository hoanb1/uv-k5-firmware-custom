#include <stdio.h>
#include <stdint.h>

float my_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float res = x;
    for (int i = 0; i < 10; i++) {
        res = 0.5f * (res + x / res);
    }
    return res;
}

#define PI 3.1415926535f

float my_atan2f(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return PI / 2.0f;
        if (y < 0.0f) return -PI / 2.0f;
        return 0.0f;
    }
    float z = y / x;
    float atan;
    // VERY crude approximation
    if (z > -1.0f && z < 1.0f) {
        atan = z / (1.0f + 0.28f * z * z);
    } else {
        if (z > 0.0f) atan = PI / 2.0f - z / (z * z + 0.28f);
        else          atan = -PI / 2.0f - z / (z * z + 0.28f);
    }
    if (x < 0.0f) {
        if (y >= 0.0f) atan += PI;
        else           atan -= PI;
    }
    return atan;
}

float my_sinf(float x) {
    // Normalize x to -PI .. PI
    while (x > PI) x -= 2.0f * PI;
    while (x < -PI) x += 2.0f * PI;
    // Bhaskara I approximation
    if (x > 0.0f) {
        return (16.0f * x * (PI - x)) / (5.0f * PI * PI - 4.0f * x * (PI - x));
    } else {
        return -(16.0f * (-x) * (PI - (-x))) / (5.0f * PI * PI - 4.0f * (-x) * (PI - (-x)));
    }
}

float my_cosf(float x) {
    return my_sinf(x + PI / 2.0f);
}

void rs41_ecef_to_lla_float(int32_t ecef_x_cm, int32_t ecef_y_cm, int32_t ecef_z_cm,
                              int32_t *lat_1e6, int32_t *lon_1e6, int32_t *alt_cm)
{
    float x = ecef_x_cm / 100.0f;
    float y = ecef_y_cm / 100.0f;
    float z = ecef_z_cm / 100.0f;
    
    const float a = 6378137.0f;
    const float e2 = 0.00669437999014f;
    const float b = 6356752.3142f;
    const float ep2 = 0.00673949674228f;
    
    float p = my_sqrtf(x*x + y*y);
    float th = my_atan2f(a*z, b*p);
    
    float lon = my_atan2f(y, x);
    float sin_th = my_sinf(th);
    float cos_th = my_cosf(th);
    
    float lat = my_atan2f(z + ep2*b*sin_th*sin_th*sin_th,
                       p - e2*a*cos_th*cos_th*cos_th);
                       
    float sin_lat = my_sinf(lat);
    float N = a / my_sqrtf(1.0f - e2*sin_lat*sin_lat);
    float alt = p / my_cosf(lat) - N;
    
    *lon_1e6 = (int32_t)(lon * (180.0f / 3.1415926535f) * 1000000.0f);
    *lat_1e6 = (int32_t)(lat * (180.0f / 3.1415926535f) * 1000000.0f);
    *alt_cm = (int32_t)(alt * 100.0f);
}

int main() {
    int32_t lat, lon, alt;
    rs41_ecef_to_lla_float(-1540000 * 100, 5740000 * 100, 2270000 * 100, &lat, &lon, &alt);
    printf("Lat: %d, Lon: %d, Alt: %d\n", lat, lon, alt);
    return 0;
}
