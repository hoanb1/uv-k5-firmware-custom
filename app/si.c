#include "si.h"
#include "board.h"
#include "misc.h"
#include "settings.h"
#include "frequencies.h"
#include "audio.h"
#include "app/fm.h"
#include "bsp/dp32g030/gpio.h"
#include "bsp/dp32g030/syscon.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/si473x.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "helper/rds.h"
#include "ui/fmradio.h"
#include "ui/helper.h"
#include "ui/inputbox.h"
#include "external/printf/printf.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FM_BT,
    MW_BT,
    SW_BT,
    LW_BT,
} BandType;

static const char SI47XX_BW_NAMES[5][6] = {
        "6 kHz", "4 kHz", "3 kHz", "2 kHz", "1 kHz",
};

static const char SI47XX_SSB_BW_NAMES[6][8] = {
        "1.2 kHz", "2.2 kHz", "3 kHz", "4 kHz", "0.5 kHz", "1 kHz",
};

static const char SI47XX_MODE_NAMES[5][4] = {
        "FM", "AM", "LSB", "USB", "CW",
};

static SI47XX_FilterBW bw = SI47XX_BW_6_kHz;
static SI47XX_SsbFilterBW ssbBw = SI47XX_SSB_BW_3_kHz;
static int8_t currentBandIndex = -1;
bool SNR_flag = true;
bool SI_run = true;

typedef struct __attribute__((packed)) {
    uint16_t frequency;
    uint8_t mode;
} SI_Preset_t;

typedef enum {
    PRESET_MODE_OFF = 0,
    PRESET_MODE_LOAD,
    PRESET_MODE_SAVE
} SI_PresetMode_t;

#define EEPROM_PRESETS_ADDR 0x1900
#define MAX_PRESETS 20

static SI_PresetMode_t gPresetState = PRESET_MODE_OFF;
static uint8_t gPresetIndex = 0;
static bool gPresetKeyWaitingRelease = false;

static void SI_SafeEEPROMWrite(uint32_t Address, const void *pBuffer, uint8_t Size) {
    const uint8_t *p = (const uint8_t *)pBuffer;
    for (uint8_t i = 0; i < Size; i++) {
        EEPROM_WriteBuffer(Address + i, p + i, 1);
    }
}

#include "app/spectrum.h"
typedef struct // Band data
{
    const char *bandName; // Bandname
//    BandType bandType;    // Band type (FM, MW or SW)
//    SI47XX_MODE prefmod;  // Pref. modulation
    uint16_t minimumFreq; // Minimum frequency of the band
    uint16_t maximumFreq; // maximum frequency of the band
//    uint16_t currentFreq; // Default frequency or current frequency
//    uint8_t currentStep;  // Default step (increment and decrement)
//    int lastBFO;          // Last BFO per band
//    int lastmanuBFO;      // Last Manual BFO per band using X-Tal

} SIBand;

SIBand bands[] = {
        {"LW",  100,   514},         //  LW          1
        {"MW",  514,   1800},       //  MW          2
        {"160M Ham", 1800,  2000},  // Ham  160M 3
        {"120M",       2000,  3200},       //      120M 4
        {"90M",        3200,  3500},        //       90M 5
        {"80M Ham",    3500,  3900},   // Ham   80M 6
        {"75M",        3900,  5300},        //       75M 7
        {"60M",        5300,  5900}, // Ham   60M   8
        {"49M",        5900,  7000},       //       49M 9
        {"40M Ham",    7000,  7500}, // Ham   40M   10
        {"41M",        7500,  9000},      //       41M 11
        {"31M",        9000,  10000}, //       31M   12
        {"30M Ham",    10000, 10200}, // Ham   30M   13
        {"25M",        10200, 13500}, //       25M   14
        {"22M",        13500, 14000}, //       22M   15
        {"20M Ham",    14000, 14500}, // Ham   20M   16
        {"19M",        14500, 17500}, //       19M   17
        {"17M",        17500, 18000}, //       17M   18
        {"16M Ham",    18000, 18500}, // Ham   16M   19
        {"15M",        18500, 21000}, //       15M   20
        {"14M Ham",    21000, 21850}, // Ham   14M   21
        {"13M",        21500, 24000}, //       13M   22
        {"12M Ham",    24000, 25500}, // Ham   12M   23
        {"11M",        25500, 26100}, //       11M   24
        {"CB",         26100, 28000},  // CB band 25
        {"10M Ham",    28000, 29750},                                                      // Ham   10M   26
        {"10M",        28000, 30000}, // Ham   10M   27
        {"SW", 100,   30000}      // Whole SW 28
};
static const uint8_t BANDS_COUNT = ARRAY_SIZE(bands);

static int8_t getCurrentBandIndex() {
    for (int8_t i = 0; i < BANDS_COUNT; ++i) {
        if (siCurrentFreq >= bands[i].minimumFreq &&
            siCurrentFreq <= bands[i].maximumFreq) {
            return i;
        }
    }
    return -1;
}

static uint8_t att = 0;
static uint16_t step = 10;

static DateTime dt;
static int16_t bfo = 0;
uint32_t light_time;
bool INPUT_STATE = false;

static void light_open() {
    if(gEeprom.BACKLIGHT_TIME) {
        light_time = (BACKLIGHT_MAP[gEeprom.BACKLIGHT_TIME-1]-1>=0?BACKLIGHT_MAP[gEeprom.BACKLIGHT_TIME-1]-1:0)*500;
        BACKLIGHT_TurnOn();
    }

}

void WaitDisplay() {
    UI_DisplayClear();
    UI_PrintStringSmall("SI4732 Wait...", 0, 127, 3);
    ST7565_BlitFullScreen();

}

static void tune(uint32_t f) {
    if (si4732mode == SI47XX_FM) {
        if (f < 6400000 || f > 10800000) {
            return;
        }
    } else {
        if (f < 15000 || f > 3000000) {
            return;
        }
    }
    EEPROM_WriteBuffer(SI4732_FREQ_ADD + si4732mode * 4, (uint8_t * ) & f, 4);

    f /= divider;
    if (si4732mode == SI47XX_FM) {
        f -= f % 5;
    }

    SI47XX_ClearRDS();

    SI47XX_SetFreq(f);
    SI47XX_SetAutomaticGainControl(att > 0, att);
    currentBandIndex = getCurrentBandIndex();
}

void SI_init() {
    SI_run = true;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    BK4819_Disable();


    SI47XX_PowerUp();

    SI47XX_SetAutomaticGainControl(att > 0, att);
}


static bool seeking = false;
static uint8_t seeking_way = 0;


static void resetBFO() {
        bfo = 0;
        SI47XX_SetBFO(bfo);

}

static void SI_SavePreset(uint8_t slot) {
    if (slot < 1 || slot > MAX_PRESETS) return;
    // Validate before writing: reject zero/invalid frequency or unknown mode
    if (siCurrentFreq == 0 || siCurrentFreq == 0xFFFF || si4732mode >= 5) {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
        return;
    }
    SI_Preset_t preset;
    preset.frequency = siCurrentFreq;
    preset.mode = si4732mode;
    uint16_t addr = EEPROM_PRESETS_ADDR + (slot - 1) * sizeof(SI_Preset_t);
    SI_SafeEEPROMWrite(addr, &preset, sizeof(SI_Preset_t));
    gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
}

static void SI_LoadPreset(uint8_t slot) {
    if (slot < 1 || slot > MAX_PRESETS) return;
    SI_Preset_t preset;
    uint16_t addr = EEPROM_PRESETS_ADDR + (slot - 1) * sizeof(SI_Preset_t);
    EEPROM_ReadBuffer(addr, &preset, sizeof(SI_Preset_t));
    
    if (preset.frequency != 0xFFFF && preset.frequency > 0 && preset.mode < 5) {
        if (preset.mode == SI47XX_FM) {
            divider = 1000;
            step = 10;
        } else {
            divider = 100;
            step = (preset.mode == SI47XX_AM) ? 5 : 1;
        }
        if (si4732mode != preset.mode) {
            SI47XX_SwitchMode(preset.mode);
        }
        tune(preset.frequency * divider);
        resetBFO();
        gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
    } else {
        gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
    }
}


void SI_deinit() {
    SI47XX_PowerDown();
    BK4819_RX_TurnOn();
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
#ifdef ENABLE_DOPPLER
    SYSCON_DEV_CLK_GATE|=(1<<22);
#endif
}

bool display_flag = 0;
KeyboardState kbds = {KEY_INVALID, KEY_INVALID, 0};


void SI4732_Display() {
    UI_DisplayClear();

    memset(gStatusLine, 0, sizeof(gStatusLine));
    if (gPresetState != PRESET_MODE_OFF) {
        if (gPresetState == PRESET_MODE_LOAD) {
            GUI_DisplaySmallest("Radio List", 44, 1, true, true);
        } else {
            GUI_DisplaySmallest("SAVE PRESET", 42, 1, true, true);
        }

        // Separator line under status line
        for (uint8_t x = 0; x < 128; x++) {
            gFrameBuffer[0][x] |= 0x01;
        }

        uint8_t start = 0;
        if (gPresetIndex >= 9) {
            start = gPresetIndex - 8;
        }

        for (uint8_t i = 0; i < 9; i++) {
            uint8_t idx = start + i;
            SI_Preset_t preset;
            uint16_t addr = EEPROM_PRESETS_ADDR + idx * sizeof(SI_Preset_t);
            EEPROM_ReadBuffer(addr, &preset, sizeof(SI_Preset_t));

            char lineStr[32];
            if (preset.frequency != 0xFFFF && preset.frequency > 0 && preset.mode < 5) {
                uint32_t div = (preset.mode == SI47XX_FM) ? 1000 : 100;
                uint32_t f = preset.frequency * div;
                uint16_t fp1 = f / 100000;
                uint16_t fp2 = f / 100 % 1000;
                sprintf(lineStr, "  %2u: %3u.%03u %s", idx + 1, fp1, fp2, SI47XX_MODE_NAMES[preset.mode]);
            } else {
                sprintf(lineStr, "  %2u: -- Empty --", idx + 1);
            }

            if (idx == gPresetIndex) {
                lineStr[0] = '>';
                lineStr[1] = ' ';
            }

            GUI_DisplaySmallest(lineStr, 10, 2 + i * 6, false, true);
        }
    } else if (INPUT_STATE) {
        UI_PrintStringSmall(freqInputString, 2, 127, 1);

    } else {
        uint8_t String[19];

        
        uint32_t f = siCurrentFreq * divider;
        uint16_t fp1 = f / 100000;
        uint16_t fp2 = f / 100 % 1000;
        sprintf(String, "%3u.%03u", fp1, fp2);
        UI_DisplayFrequency(String, 64 - strlen(String) * 13 / 2, 2, false);
        
        const uint8_t BASE = 38;
        GUI_DisplaySmallest(SI47XX_MODE_NAMES[si4732mode], LCD_WIDTH - 12, BASE - 10 - 8, false, true);


        if (SI47XX_IsSSB()) {
            sprintf(String, "%d", bfo);
            GUI_DisplaySmallest(String, LCD_WIDTH - strlen(String) * 4, BASE - 8, false, true);
        }

        if (si4732mode == SI47XX_FM) {
            if (rds.RDSSignal) {
                GUI_DisplaySmallest("RDS", LCD_WIDTH - 12, 12 - 8, false, true);
            }

            char genre[17];
            const char wd[8][3] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA", "SU"};
            SI47XX_GetProgramType(genre);

            GUI_DisplaySmallest(genre, 64 - strlen(genre) * 2, 15 - 8, false, true);


            if (SI47XX_GetLocalDateTime(&dt)) {
                sprintf(String, "%02u.%02u.%04u, %s %02u:%02u", dt.day, dt.month, dt.year, wd[dt.wday], dt.hour,
                        dt.minute);
                GUI_DisplaySmallest(String, 64 - strlen(String) * 2, 22 - 8, false, true);

            }
            GUI_DisplaySmallest(rds.radioText, 0, LCD_HEIGHT - 8 - 8, false, true);
        }

        if (si4732mode == SI47XX_FM) {
            sprintf(String, "STP %u ATT %u", step, att);
        } else if (SI47XX_IsSSB()) {
            sprintf(String, "STP %u ATT %u BW %s", step, att, SI47XX_SSB_BW_NAMES[ssbBw]);
        } else {
            sprintf(String, "STP %u ATT %u BW %s", step, att, SI47XX_BW_NAMES[bw]);
        }
        GUI_DisplaySmallest(String, 64 - strlen(String) * 2, BASE + 6 - 8, false, true);
        if (si4732mode != SI47XX_FM) {
            if (currentBandIndex >= 0) {
                sprintf(String, "%s %d - %dkHz", bands[currentBandIndex].bandName, bands[currentBandIndex].minimumFreq,
                        bands[currentBandIndex].maximumFreq);
                GUI_DisplaySmallest(String, 64 - strlen(String) * 2, LCD_HEIGHT - 5 - 9, false, true);
            }
        }
        if (SNR_flag) {
            uint8_t rssi = rsqStatus.resp.RSSI;
            if (rssi > 64) {
                rssi = 64;
            }
            for (int i = 0; i < rssi * 2; ++i) {
                gFrameBuffer[0][i] |= 0b00111100;
            }


            sprintf(String, "SNR %u", rsqStatus.resp.SNR);

            GUI_DisplaySmallest(String, 0, 15 - 8, false, true);
        }
    }

    ST7565_BlitFullScreen();
    if (gPresetState != PRESET_MODE_OFF) {
        ST7565_BlitStatusLine();
    }
}


static void OnKeyDownFreqInput(uint8_t key) {
    switch (key) {
        case KEY_0:
        case KEY_1:
        case KEY_2:
        case KEY_3:
        case KEY_4:
        case KEY_5:
        case KEY_6:
        case KEY_7:
        case KEY_8:
        case KEY_9:
        case KEY_STAR:
            UpdateFreqInput(key);
            break;
        case KEY_EXIT:
            if (freqInputIndex == 0) {
                INPUT_STATE = false;
                break;
            }
            UpdateFreqInput(key);

            break;
        case KEY_MENU:
            if (!FreqCheck(tempFreq)) {
                break;
            }
            INPUT_STATE = false;
            tune(tempFreq);
            resetBFO();

            break;
        default:
            break;
    }
}


void HandleUserInput() {
    kbds.prev = kbds.current;
    kbds.current = GetKey();
    bool KEY_TYPE1 = false, KEY_TYPE2 = false, KEY_TYPE3 = false;
    
    if (kbds.current == KEY_INVALID) {
        if (kbds.counter > 2) {
            if (kbds.counter >= 10) {
                KEY_TYPE1 = true;
            } else {
                KEY_TYPE3 = true;
            }
        }
        kbds.counter = 0;
    } else {
        if (kbds.counter >= 6 && kbds.counter % 2 == 1) {
            if (kbds.current != KEY_STAR) {
                KEY_TYPE1 = true;
            }
        }
        if (kbds.current == kbds.prev) {
            
            if (kbds.counter <= 14) {

                KEY_TYPE2 = true;
                kbds.counter++;
            }
        } else {
            
            kbds.counter = 1;
        }
        SYSTEM_DelayMs(20);
    }

    if (KEY_TYPE1 || KEY_TYPE2 || KEY_TYPE3) {
        light_open();
        display_flag = 1;
    }
     SI_key(kbds.current, KEY_TYPE1, KEY_TYPE2, KEY_TYPE3, kbds.prev);

}

void SI_key(KEY_Code_t key, bool KEY_TYPE1, bool KEY_TYPE2, bool KEY_TYPE3, KEY_Code_t key_prev) {
    if (gPresetState != PRESET_MODE_OFF) {
        if (gPresetKeyWaitingRelease) {
            if (key == KEY_INVALID) {
                gPresetKeyWaitingRelease = false;
            }
            return;
        }
        KEY_Code_t k = KEY_TYPE3 ? key_prev : key;
        if (KEY_TYPE1 || KEY_TYPE3) {
            switch (k) {
                case KEY_UP:
                    if (gPresetIndex > 0) {
                        gPresetIndex--;
                    } else {
                        gPresetIndex = MAX_PRESETS - 1;
                    }
                    display_flag = true;
                    break;
                case KEY_DOWN:
                    if (gPresetIndex < MAX_PRESETS - 1) {
                        gPresetIndex++;
                    } else {
                        gPresetIndex = 0;
                    }
                    display_flag = true;
                    break;
                case KEY_MENU:
                    if (gPresetState == PRESET_MODE_LOAD) {
                        SI_LoadPreset(gPresetIndex + 1);
                    } else {
                        SI_SavePreset(gPresetIndex + 1);
                    }
                    gPresetState = PRESET_MODE_OFF;
                    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
                    display_flag = true;
                    break;
                case KEY_EXIT:
                    SI_run = false;
                    break;
                case KEY_STAR:
                    gPresetState = PRESET_MODE_OFF;
                    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
                    display_flag = true;
                    break;
                case KEY_0:
                    gPresetState = PRESET_MODE_OFF;
                    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
                    divider = 1000;
                    SI47XX_SwitchMode(SI47XX_FM);
                    step = 10;
                    tune(Read_FreqSaved());
                    resetBFO();
                    display_flag = true;
                    break;
                case KEY_1:
                case KEY_2:
                case KEY_3:
                case KEY_4:
                case KEY_5:
                case KEY_6:
                case KEY_7:
                case KEY_8:
                case KEY_9:
                    gPresetIndex = k - KEY_1;
                    display_flag = true;
                    break;
                default:
                    break;
            }
        }
        return;
    }
    // up-down keys
    if (INPUT_STATE && KEY_TYPE3) {
        OnKeyDownFreqInput(key_prev);
        return ;
    }
    if (KEY_TYPE1 || KEY_TYPE3) {
        if (KEY_TYPE3 || key == KEY_INVALID)key = key_prev;
        switch (key) {
            case KEY_UP:
            case KEY_DOWN:
                tune((siCurrentFreq + (key == KEY_UP ? step : -step)) * divider);
                resetBFO();
                return ;
#ifdef ENABLE_4732SSB
                case KEY_SIDE1:
                case KEY_SIDE2:
                    if (SI47XX_IsSSB()) {
                        if (key == KEY_SIDE1 ? (bfo < INT16_MAX - 10) : (bfo > INT16_MIN + 10)) {
                            bfo = bfo + (key == KEY_SIDE1 ? 10 : -10);
                        }
                        SI47XX_SetBFO(bfo);
                    }
                    return ;

#endif
            case KEY_2:
            case KEY_8:
                if (key == KEY_2 ? att < 37 : att > 0) {
                    key == KEY_2 ? att++ : att--;
                    SI47XX_SetAutomaticGainControl(key == KEY_2 ? 1 : att > 0, att);
                }
                return ;

            case KEY_MENU:
            case KEY_STAR:
                if (KEY_TYPE3) {
                    gPresetState = PRESET_MODE_SAVE;
                    gPresetIndex = 0;
                    gPresetKeyWaitingRelease = true;
                    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
                    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                }
                return ;

            default:
                break;
        }
    }

    // Simple keypress
    if (KEY_TYPE3) {

        switch (key_prev) {
            case KEY_4:
                SNR_flag = !SNR_flag;
                return ;
            case KEY_1:
                if (step < 1000) {
                    if (step == 1 || step == 10 || step == 100 ) {
                        step *= 5;
                    } else {
                        step *= 2;
                    }
                }
                return ;


            case KEY_7:
                if (step > 1) {
                    if ( step == 10 || step == 100 || step == 1000) {
                        step /= 2;
                    } else {
                        step /= 5;
                    }
                }
                return ;

            case KEY_6:
#ifdef ENABLE_4732SSB

                if (SI47XX_IsSSB()) {
                                    if (ssbBw == SI47XX_SSB_BW_1_0_kHz) {
                                        ssbBw = SI47XX_SSB_BW_1_2_kHz;
                                    } else {
                                        ssbBw++;
                                    }
                                    SI47XX_SetSsbBandwidth(ssbBw);
                                } else {
#endif
                if (bw == SI47XX_BW_1_kHz) {
                    bw = SI47XX_BW_6_kHz;
                } else {
                    bw++;
                }
                SI47XX_SetBandwidth(bw, true);
#ifdef ENABLE_4732SSB

                }
#endif

                return ;

            case KEY_5:
                INPUT_STATE = 1;
                FreqInput();
                return ;
            case KEY_0:
                WaitDisplay();
                if (si4732mode == SI47XX_FM) {
                    divider = 100;
                    SI47XX_SwitchMode(SI47XX_AM);
                    SI47XX_SetBandwidth(bw, true);
                    step = 5;
                }
#ifdef ENABLE_4732SSB
                else if (si4732mode == SI47XX_AM) {
                    divider = 100;
                    SI47XX_SwitchMode(SI47XX_LSB);
                    SI47XX_SetSsbBandwidth(ssbBw);
                    step = 1;
                }
                else if (si4732mode == SI47XX_LSB) {
                    divider = 100;
                    SI47XX_SwitchMode(SI47XX_USB);
                    SI47XX_SetSsbBandwidth(ssbBw);
                    step = 1;
                }
#endif
                else {
                    divider = 1000;
                    SI47XX_SwitchMode(SI47XX_FM);
                    step = 10;
                }
                tune(Read_FreqSaved());
                resetBFO();
                return ;
#ifdef ENABLE_4732SSB

                case KEY_F:
                    if (SI47XX_IsSSB()) {
                        uint32_t tmpF;
                        SI47XX_SwitchMode(si4732mode == SI47XX_LSB ? SI47XX_USB : SI47XX_LSB);
                        tune(Read_FreqSaved()); // to apply SSB
                        return ;
                    }
#endif

            case KEY_EXIT:
                if (seeking) {
                    SI47XX_PowerDown();
                    SI47XX_PowerUp();
                    seeking = false;
                } else {
                    gPresetState = PRESET_MODE_LOAD;
                    gPresetIndex = 0;
                    gPresetKeyWaitingRelease = true;
                }
                return ;
            case KEY_3:
            case KEY_9:
#ifdef ENABLE_4732SSB

                if (SI47XX_IsSSB()) {
                                    return ;
                                }
#endif
                if (si4732mode == SI47XX_FM) {
                    SI47XX_SetSeekFmSpacing(step);
                } else {
                    SI47XX_SetSeekAmSpacing(step);
                }

                SI47XX_Seek(key == KEY_3 ? 1 : 0, 1);
                if (key == KEY_3)seeking_way = 1;
                else seeking_way = 0;


                seeking = true;
                return ;
            default:
                break;
        }
    }
    return ;
}


void SI4732_Main() {
#ifdef ENABLE_DOPPLER
    SYSCON_DEV_CLK_GATE= SYSCON_DEV_CLK_GATE & ( ~(1 << 22));
#endif

    gPresetState = PRESET_MODE_LOAD;
    gPresetIndex = 0;
    gPresetKeyWaitingRelease = true;
    light_open();
    SI_init();

    uint16_t cnt = 500;
    while (SI_run) {
        if (light_time && gEeprom.BACKLIGHT_TIME != 7) {
            light_time--;
            if (light_time == 0)BACKLIGHT_TurnOff();
        }
        if (cnt == 500) {
            DrawPower();
            ST7565_BlitStatusLine();
            cnt = 0;

            if (si4732mode == SI47XX_FM) {
                if (SI47XX_GetRDS()) display_flag = 1;
            }
            if (SNR_flag) {
                RSQ_GET();
                display_flag = 1;
            }
        }

        if (cnt % 25 == 0) {
          HandleUserInput();
        }

        if (seeking && cnt % 100 == 0) {
            UI_PrintStringSmallBuffer("*", gStatusLine);
            bool valid = false;
            siCurrentFreq = SI47XX_getFrequency(&valid);
            uint32_t f = siCurrentFreq * divider;
            EEPROM_WriteBuffer(SI4732_FREQ_ADD + si4732mode * 4, (uint8_t * ) & f, 4);

            if (valid) {
                seeking = false;
                light_open();
                tune((siCurrentFreq) * divider);
            }

            display_flag = 1;
        }
        cnt++;
        if (display_flag) {
            display_flag = 0;
            SI4732_Display();
        }
        SYSTEM_DelayMs(1);
    }
    SI_deinit();

}
