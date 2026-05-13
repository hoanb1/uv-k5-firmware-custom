#include <stdint.h>

float sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float res = x;
    for (int i = 0; i < 10; i++) {
        res = 0.5f * (res + x / res);
    }
    return res;
}

#define PI 3.1415926535f

float atan2f(float y, float x) {
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

float sinf(float x) {
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

float cosf(float x) {
    return sinf(x + PI / 2.0f);
}
