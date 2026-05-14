#include "doppler.h"
#include "string.h"
#include "driver/eeprom.h"
#include "bsp/dp32g030/rtc.h"
#include "ui/helper.h"

struct satellite_t satellite;
struct satellite_d satellite_data;
bool DOPPLER_FLAG = true;

//
//






//






//

//


#include <stdint.h>

void uint16_to_uint8_array(uint16_t value, uint8_t array[2]) {
    array[0] = value & 0xFF;        
    array[1] = (value >> 8) & 0xFF; 
}

void INIT_DOPPLER_DATA() {
    memset(&satellite, 0, sizeof(satellite));
    EEPROM_ReadBuffer(0x02BA0, &satellite, sizeof(satellite));
    if (satellite.name[9] != 0 ||
        !(satellite.name[0] >= 32 && satellite.name[0] <= 126)
            ) {
        DOPPLER_FLAG = 0;
        return;
    }

    for (int i = strlen(satellite.name); i < 10; i++)
        if (satellite.name[i] != 0) {
            DOPPLER_FLAG = 0;
            return;
        }


}


int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}


int days_in_month(int year, int month) {
    int days[] = {31, 28 + is_leap_year(year), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1];
}


int32_t UNIX_TIME(uint8_t time2[6]) {
    
    int32_t seconds = 0;
    
    for (int year = 0; year < time2[0]; year++) {
        seconds += (is_leap_year(year) + 365) * 24 * 3600;
    }

    
    for (int month = 1; month < time2[1]; month++) {
        seconds += days_in_month(time2[0], month) * 24 * 3600;
    }
    
    seconds += (time2[2] - 1) * 24 * 3600;
    seconds += time2[3] * 3600;
    seconds += time2[4] * 60;
    seconds += time2[5];
    return seconds;
}

void READ_DATA(int32_t time_diff, int32_t time_diff1) {
    int32_t n = -time_diff;
    if (time_diff <= 0 && time_diff1 >= 0)
    {

        if ((n & 0x01) != 0)return;
        n = n >> 1;
    } else n = 0;

    EEPROM_ReadBuffer(0x1E200 + (n << 3), &satellite_data, sizeof(satellite_data));






//










}