#include <stdio.h>
#include <stdint.h>
#include <math.h>

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
    
    float p = sqrtf(x*x + y*y);
    
    float th_y = a*z;
    float th_x = b*p;
    float th_hyp = sqrtf(th_x*th_x + th_y*th_y);
    float sin_th = th_y / th_hyp;
    float cos_th = th_x / th_hyp;
    
    float lat_y = z + ep2*b*sin_th*sin_th*sin_th;
    float lat_x = p - e2*a*cos_th*cos_th*cos_th;
    float lat = atan2f(lat_y, lat_x);
    
    float lat_hyp = sqrtf(lat_x*lat_x + lat_y*lat_y);
    float sin_lat = lat_y / lat_hyp;
    float cos_lat = lat_x / lat_hyp;
    
    float N = a / sqrtf(1.0f - e2*sin_lat*sin_lat);
    float alt = p / cos_lat - N;
    
    float lon = atan2f(y, x);
    
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
