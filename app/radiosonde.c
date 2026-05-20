/* Radiosonde Application Module for UV-K5
 * Self-contained main loop (like APP_RunSpectrum)
 * Handles BK4819 tuning, bit recovery simulation, and RS41 decoding
 */

#include "radiosonde.h"
#include "rs41.h"
#include "../driver/eeprom.h"

typedef struct {
    uint32_t frequency;
    int32_t lat_1e6;
    int32_t lon_1e6;
    int32_t alt_cm;
} SavedSonde_t;



#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../driver/bk4819.h"
#include "../driver/uart.h"
#include "../driver/bk4819-regs.h"
#include "../driver/st7565.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../driver/keyboard.h"
#include "../driver/gpio.h"
#include "../driver/backlight.h"
#include "../misc.h"
#include "../radio.h"
#include "../settings.h"
#include "../ui/helper.h"
#include "../board.h"
#include "../driver/adc.h"
#include "../external/printf/printf.h"
#include "../font.h"
#include "../audio.h"
#include "../bsp/dp32g030/gpio.h"
#include "../bsp/dp32g030/portcon.h"
#include "../bsp/dp32g030/saradc.h"
#include "ARMCM0.h"
#include "../ui/ui.h"

// Default RS41 frequency range (400.000 - 406.000 MHz)
// BK4819 uses freq/10 format
#define SONDE_FREQ_START   40000000   // 400.000 MHz
#define SONDE_FREQ_END     40600000   // 406.000 MHz
#define SONDE_FREQ_DEFAULT 40300000   // 403.000 MHz default

static const uint32_t SONDE_STEPS[] = { 100000, 10000, 1000, 100 };
static void Sonde_DrawPixel(int x, int y, bool black);

#define SONDE_SCAN_DWELL_MS   2000   // 2s per channel (RS41 transmits every 1s)
#define SONDE_RSSI_THRESHOLD  500    // RSSI threshold for signal detect
#define SONDE_TIMEOUT_FRAMES  300    // ~30 seconds at 10 fps

SondeApp_t gSondeApp;

// ============================================================
// Display helpers
// ============================================================

static void Sonde_DrawStatusBar(void) {
    // Clear status line
    memset(gStatusLine, 0, sizeof(gStatusLine));

    // Title
    UI_PrintStringSmallBuffer("SONDE", gStatusLine);

    // History Bar (Center)
    char hstr[12];
    uint8_t ptr = gSondeApp.history_ptr;
    for (int i = 0; i < 8; i++) {
        hstr[i] = (char)gSondeApp.history[(ptr + i) % 10];
    }
    hstr[8] = '\0';
    UI_PrintStringSmallBuffer(hstr, gStatusLine + 36);

    // RSSI Bar
    // Map RSSI (0-1000) to pixels (0-24)
    int rssi_len = (gSondeApp.last_rssi * 24) / 1000;
    if (rssi_len > 24) rssi_len = 24;
    for (int i = 0; i < rssi_len; i++) {
        for (int j = 1; j < 5; j++) {
            Sonde_DrawPixel(100 + i, j, true);
        }
    }
}


static void Sonde_DrawDiagnostic(void)
{
    char str[32];
    GUI_DisplaySmallest("--- DIAGNOSTICS ---", 0, 8, false, true);
    
    sprintf(str, "RSSI: %d", gSondeApp.last_rssi);
    GUI_DisplaySmallest(str, 0, 16, false, true);
    
    sprintf(str, "ADC P2P: %u", gSondeApp.last_adc_p2p);
    GUI_DisplaySmallest(str, 0, 24, false, true);
    
    sprintf(str, "DC Offs: %u", gSondeApp.adc_avg_x512 >> 9);
    GUI_DisplaySmallest(str, 0, 32, false, true);

    const char* g_modes[] = {"AUTO", "LOW", "MID", "HIGH", "MAX"};
    sprintf(str, "Gain Mode: %s", g_modes[gSondeApp.gain_mode]);
    GUI_DisplaySmallest(str, 64, 32, false, true);

    const char *state_str = "SEARCHING";
    if (gSondeApp.decoder.state == RS41_STATE_COLLECT_FRAME) state_str = "COLLECTING";
    sprintf(str, "State: %s", state_str);
    GUI_DisplaySmallest(str, 0, 40, false, true);

    // Alerts (Line 6)
    if (gSondeApp.last_adc_p2p > 3800 || (gSondeApp.adc_avg_x512 >> 9) > 3500) {
        GUI_DisplaySmallest("!! CLIPPING !!", 0, 48, false, true);
    } else if (gSondeApp.timeout_frames > 30) {
        GUI_DisplaySmallest("!! NO SIGNAL 30s !!", 0, 48, false, true);
    }
}

static void Sonde_DrawData(const RS41_Data_t *d)
{
    char str[32];

    // Line 1: Sonde ID
    sprintf(str, "ID: %.8s", d->sonde_id);
    GUI_DisplaySmallest(str, 0, 8, false, true);

    // Line 2: Latitude
    int32_t lat_deg = d->lat_1e6 / 1000000;
    int32_t lat_frac = d->lat_1e6 % 1000000;
    if (lat_frac < 0) lat_frac = -lat_frac;
    char lat_dir = (d->lat_1e6 >= 0) ? 'N' : 'S';
    sprintf(str, "Lat: %ld.%04ld %c", (long)lat_deg, (long)(lat_frac / 100), lat_dir);
    GUI_DisplaySmallest(str, 0, 16, false, true);

    // Line 3: Longitude
    int32_t lon_deg = d->lon_1e6 / 1000000;
    int32_t lon_frac = d->lon_1e6 % 1000000;
    if (lon_frac < 0) lon_frac = -lon_frac;
    char lon_dir = (d->lon_1e6 >= 0) ? 'E' : 'W';
    sprintf(str, "Lon: %ld.%04ld %c", (long)lon_deg, (long)(lon_frac / 100), lon_dir);
    GUI_DisplaySmallest(str, 0, 24, false, true);

    // Line 4: Altitude and AGC Gain
    int32_t alt_m = d->alt_cm / 100;
    const char* g_modes[] = {"AUTO", "LOW", "MID", "HIGH", "MAX"};
    sprintf(str, "Alt: %ldm   G:%s", (long)alt_m, g_modes[gSondeApp.gain_mode]);
    GUI_DisplaySmallest(str, 0, 32, false, true);

    // Line 5: Velocity
    int h_spd = d->vH_cm / 100;
    int v_spd = d->vV_cm / 100;
    sprintf(str, "Vel: H:%dm/s V:%dm/s", h_spd, v_spd);
    GUI_DisplaySmallest(str, 0, 40, false, true);

    // Line 6: GPS Satellites, Time, Battery
    sprintf(str, "Sat:%2u %02u:%02u:%02u %u.%uV", 
            d->numSV, d->gps_hour, d->gps_min, d->gps_sec,
            d->batt_mv / 1000, (d->batt_mv % 1000) / 100);
    GUI_DisplaySmallest(str, 0, 48, false, true);
}

static void Sonde_DrawPixel(int x, int y, bool black) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    
    if (y < 8) {
        // Line 0 is gStatusLine
        if (black) {
            gStatusLine[x] |= (1 << y);
        } else {
            gStatusLine[x] &= ~(1 << y);
        }
    } else {
        // Lines 1-7 are gFrameBuffer
        int line = (y / 8) - 1;
        int bit = y % 8;
        if (black) {
            gFrameBuffer[line][x] |= (1 << bit);
        } else {
            gFrameBuffer[line][x] &= ~(1 << bit);
        }
    }
}


static void Sonde_Render(void)
{
    UI_DisplayClear();
    memset(gStatusLine, 0, sizeof(gStatusLine));
    
    // ALWAYS DRAW FREQUENCY AND STEP AT y=0 BEFORE ANYTHING ELSE
    char fstr[32];
    const char *step_str = (gSondeApp.step_idx == 0) ? "1M" : 
                           (gSondeApp.step_idx == 1) ? "100k" :
                           (gSondeApp.step_idx == 2) ? "10k" : "1k";
    sprintf(fstr, "FREQ: %3u.%03u  %s", 
            gSondeApp.frequency / 100000, 
            (gSondeApp.frequency % 100000) / 100, 
            step_str);
    GUI_DisplaySmallest(fstr, 0, 0, false, true);

    Sonde_DrawStatusBar();

    const RS41_Data_t *d = RS41_GetData(&gSondeApp.decoder);

    if (gSondeApp.mode == SONDE_MODE_QR) {
        
    } else if (gSondeApp.mode == SONDE_MODE_DIAGNOSTIC) {
        Sonde_DrawDiagnostic();
    } else {
        Sonde_DrawData(d);
    }

    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
}

// ============================================================
// BK4819 setup for RS41 reception
// ============================================================

static void Sonde_SetupReceiver(uint32_t freq)
{
    gSondeApp.frequency = freq;

    // Disable all interrupts and squelch
    BK4819_WriteRegister(BK4819_REG_3F, 0);

    // Set frequency
    BK4819_SetFrequency(freq);
    // BK4819_REG_30: Power management. 
    BK4819_WriteRegister(BK4819_REG_30, 0); 
    BK4819_WriteRegister(BK4819_REG_30, 0xBFF1); 

    // FM narrow demod, wider RF bandwidth for GFSK signal
    // RS41 uses 4800 baud FSK with 4.8kHz peak-to-peak deviation.
    // 12.5kHz or 25kHz filter is required (25kHz recommended for stability without AFC)
    BK4819_SetFilterBandwidth(BK4819_FILTER_BW_WIDE, true);

    // Override REG_43 for maximum audio bandwidth for RS41 4800-baud GFSK:
    // <14:12>=7 RF BW = 4.5kHz * 2 (bit<5>=1) = 9kHz  
    // <11:9> =7 RF BW weak = 4.5kHz * 2 = 9kHz
    // <8:6>  =4 AFTxLPF2 = 4.5kHz (widest available)
    // <5:4>  =2 BW Mode = 25k/20k
    // <3>    =1 (fixed)
    // <2>    =0 Normal gain after FM demod (no clipping)
    // 0 111 111 100 10 1 0 00 = 0x7F28
    BK4819_WriteRegister(0x43, 
        (7u << 12) |  // RF filter bandwidth = 4.5kHz * 2
        (7u << 9)  |  // RF filter bandwidth weak = 4.5kHz * 2
        (4u << 6)  |  // AFTxLPF2 = 4.5kHz
        (2u << 4)  |  // 25k BW mode
        (1u << 3)  |  // fixed
        (0u << 2)  |  // Normal gain (was +6dB, caused ADC clipping)
        (0u << 0));   // reserved

    // (Removed GPIO0 override so it correctly works as RX_ENABLE)

    // Ensure AF output is not muted and has maximum gain for testing
    // REG_48: <13> = Mute, <6:0> = Gain (0-127)
    uint16_t reg_48 = BK4819_ReadRegister(0x48);
    reg_48 &= ~(1u << 13); // Unmute
    reg_48 = (reg_48 & ~0x007F) | 60; // Moderate AF gain to prevent clipping
    BK4819_WriteRegister(0x48, reg_48);

    // Set PGA Gain (Reg 0x13): bit 0-6 is LNA/PGA gain.
    // Setting to 0x20 to further reduce clipping risk.
    BK4819_WriteRegister(0x13, 0x0020);

    // Disable RX AF filters for raw FSK data:
    uint16_t reg_2b = BK4819_ReadRegister(0x2B);
    reg_2b |= (1u << 10) | (1u << 9) | (1u << 8); // Disable high-pass, low-pass, de-emphasis
    BK4819_WriteRegister(0x2B, reg_2b);

    // Disable squelch
    BK4819_WriteRegister(BK4819_REG_4D, 0);
    BK4819_WriteRegister(BK4819_REG_4E, 0x206F);

    // Setup AGC for sensitive reception
    BK4819_InitAGC(false);
    BK4819_SetAGC(true);

    // Enable RX
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    // Ensure audio path stays on and AF is routed
    gEnableSpeaker = true;
    BK4819_SetAF(BK4819_AF_FM);
}

// ============================================================
// Read AF level from BK4819 for bit recovery
// Returns 1 or 0 based on FM demodulated output polarity
// ============================================================

static uint8_t Sonde_ReadBit(void)
{
    // REG_69: AF output level after FM demod
    // Bit 15 gives sign of FM deviation (frequency above or below center)
    uint16_t reg69 = BK4819_ReadRegister(0x69);
    return (reg69 & 0x8000) ? 1 : 0;
}

// ============================================================
// Keyboard handling
// Returns true if should exit
// ============================================================

static bool Sonde_HandleKeys(void)
{
    KEY_Code_t key = KEYBOARD_Poll();

    if (key == KEY_INVALID)
        return false;

    // Debounce
    SYSTEM_DelayMs(50);

    switch (key) {
        case KEY_EXIT:
            return true;

        case KEY_UP:
            gSondeApp.frequency += SONDE_STEPS[gSondeApp.step_idx];
            if (gSondeApp.frequency > SONDE_FREQ_END)
                gSondeApp.frequency = SONDE_FREQ_START;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_DOWN:
            if (gSondeApp.frequency <= SONDE_FREQ_START + SONDE_STEPS[gSondeApp.step_idx])
                gSondeApp.frequency = SONDE_FREQ_END;
            else
                gSondeApp.frequency -= SONDE_STEPS[gSondeApp.step_idx];
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_STAR:
            // Cycle step
            gSondeApp.step_idx = (gSondeApp.step_idx + 1) % 4;
            break;

        case KEY_0: // 400.000 MHz
            gSondeApp.frequency = 40000000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;
        case KEY_1: // 401.000 MHz
            gSondeApp.frequency = 40100000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;
        case KEY_2: // 402.000 MHz
            gSondeApp.frequency = 40200000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;
        case KEY_3: // 403.000 MHz
            gSondeApp.frequency = 40300000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;
        case KEY_4: // 404.000 MHz
            gSondeApp.frequency = 40400000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;
        case KEY_5: // 405.000 MHz
            gSondeApp.frequency = 40500000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;
        case KEY_6: // 406.000 MHz
            gSondeApp.frequency = 40600000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_7:
            gSondeApp.mode = SONDE_MODE_DIAGNOSTIC;
            break;
        case KEY_8:
            gSondeApp.mode = SONDE_MODE_MONITOR;
            break;
        case KEY_9:
            gSondeApp.mode = SONDE_MODE_QR;
            break;

        case KEY_MENU:
            // Cycle Gain mode: Auto -> Low -> Mid -> High -> Max
            gSondeApp.gain_mode = (gSondeApp.gain_mode + 1) % 5;
            // Immediate force AGC logic reset to apply new gain immediately
            gSondeApp.agc_counter = 10;
            break;

        case KEY_F:
            // Toggle audio output
            gSondeApp.audio_enabled = !gSondeApp.audio_enabled;
            if (gSondeApp.audio_enabled) {
                AUDIO_AudioPathOn();
            } else {
                AUDIO_AudioPathOff();
            }
            break;

        default:
            break;
    }

    return false;
}

// SCAN MODE HAS BEEN REMOVED

// ============================================================
// Main application loop
// Takes over from main loop, similar to APP_RunSpectrum()
// ============================================================

void APP_RunRadiosonde(void)
{
    // Initialize application state
    memset(&gSondeApp, 0, sizeof(SondeApp_t));
    gSondeApp.decoder.data.valid = false;
    gSondeApp.timeout_frames = 0;
    gSondeApp.agc_counter = 0;
    gSondeApp.mode = SONDE_MODE_DIAGNOSTIC;
    gSondeApp.frequency = SONDE_FREQ_DEFAULT;
    gSondeApp.step_idx = 1; // 100 kHz default
    gSondeApp.gain_mode = 0; // Auto Gain
    gSondeApp.audio_enabled = true;
    memset(gSondeApp.history, ' ', 10);
    gSondeApp.history_ptr = 0;
    gSondeApp.pending_history = '.';
    gSondeApp.history_sample_acc = 0;

    uint16_t save_cooldown = 0;

    // Restore last saved radiosonde state from EEPROM (0x1F70)
    SavedSonde_t saved;
    EEPROM_ReadBuffer(0x1F70, &saved, sizeof(saved));
    if (saved.frequency >= SONDE_FREQ_START && saved.frequency <= SONDE_FREQ_END) {
        gSondeApp.frequency = saved.frequency;
        if (saved.lat_1e6 >= -90000000 && saved.lat_1e6 <= 90000000 &&
            saved.lon_1e6 >= -180000000 && saved.lon_1e6 <= 180000000 &&
            saved.lat_1e6 != 0 && saved.lon_1e6 != 0) {
            gSondeApp.decoder.data.valid = true;
            gSondeApp.decoder.data.lat_1e6 = saved.lat_1e6;
            gSondeApp.decoder.data.lon_1e6 = saved.lon_1e6;
            gSondeApp.decoder.data.alt_cm = saved.alt_cm;
            strcpy(gSondeApp.decoder.data.sonde_id, "LAST GPS");
            gSondeApp.mode = SONDE_MODE_MONITOR;
        }
    }

    // ADC setup for signal monitoring (MCU Pin 9 - PA8 / UART1_RX -> ADC_CH3)
    PORTCON_PORTA_IE &= ~PORTCON_PORTA_IE_A8_MASK;
    PORTCON_PORTA_PU &= ~PORTCON_PORTA_PU_A8_MASK;  // Disable Pull-Up
    PORTCON_PORTA_PD |= PORTCON_PORTA_PD_A8_MASK;   // Enable Pull-Down only to lower bias
    PORTCON_PORTA_SEL1 &= ~PORTCON_PORTA_SEL1_A8_MASK;
    PORTCON_PORTA_SEL1 |= PORTCON_PORTA_SEL1_A8_BITS_SARADC_CH3;

    RS41_Init(&gSondeApp.decoder);

    // Enable backlight and setup receiver
    BACKLIGHT_TurnOn();
    Sonde_SetupReceiver(gSondeApp.frequency);
    AUDIO_AudioPathOn();
    Sonde_Render();

    // Main loop — runs until user presses EXIT
    while (!gSondeApp.exit_requested) {

        // Handle keyboard inputs
        if (Sonde_HandleKeys()) {
            gSondeApp.exit_requested = true;
            break;
        }

        if (gSondeApp.mode != SONDE_MODE_QR) {
            // Configure ADC to read CH3 (MCU Pin 9 - PA8)
            SARADC_CFG = (SARADC_CFG & ~SARADC_CFG_CH_SEL_MASK) | 
                         ((ADC_CH3 << SARADC_CFG_CH_SEL_SHIFT) & SARADC_CFG_CH_SEL_MASK);

            uint32_t sample_count = 0;
            bool frame_decoded = false;
            uint16_t local_min = 0xFFFF;
            uint16_t local_max = 0;

            // Zero-crossing state
            int par_alt = 1;
            uint32_t samples_since_cross = 0;
            static int32_t centered_old = 0;

            uint32_t start_val = SysTick->VAL;

            // Inner bit-recovery loop (processes up to 1 second of samples)
            while (1) {
                sample_count++;
                
                // Read ADC sample
                ADC_Start();
                while (!ADC_CheckEndOfConversion(ADC_CH3)) {}
                uint16_t adc_val = ADC_GetValue(ADC_CH3);
                
                // Track min/max for P2P amplitude
                if (adc_val < local_min) local_min = adc_val;
                if (adc_val > local_max) local_max = adc_val;

                // Dynamic DC threshold (slow EMA, alpha = 1/256)
                gSondeApp.last_adc_raw = adc_val;
                if (gSondeApp.adc_avg_x512 == 0) gSondeApp.adc_avg_x512 = (uint32_t)adc_val << 9;
                gSondeApp.adc_avg_x512 = (gSondeApp.adc_avg_x512 * 255 + ((uint32_t)adc_val << 9)) >> 8;
                
                int32_t centered = (int32_t)adc_val - (int32_t)(gSondeApp.adc_avg_x512 >> 9);
                gSondeApp.last_amplitude = (uint16_t)((centered > 0) ? centered : -centered);

                // Simple 2-sample LPF to smooth out noise spikes without losing bit edges
                int32_t filtered = (centered + centered_old) >> 1;
                centered_old = centered;

                // Hysteresis for Zero-crossing
                int current_par = (filtered > 100) ? 1 : ((filtered < -100) ? -1 : par_alt);

                // Zero crossing logic (DPLL)
                if (current_par * par_alt <= 0) {
                    // 38400 Hz sampling / 4800 baud = 8 samples per bit
                    int len = (samples_since_cross + 4) / 8;
                    int bit = (par_alt > 0) ? 1 : 0;
                    
                    for (int i = 0; i < len; i++) {
                        if (RS41_ProcessBit(&gSondeApp.decoder, bit)) {
                            frame_decoded = true;
                            break;
                        }
                    }
                    
                    samples_since_cross = 0;
                    par_alt = current_par;
                }
                samples_since_cross++;

                // Breakout conditions
                if (frame_decoded) break;
                // 8x oversampling: 200ms = 7680 samples, 1s = 38400 samples
                if (gSondeApp.decoder.state != RS41_STATE_COLLECT_FRAME && sample_count > 7680) break;
                if (sample_count > 38400) break;

                // Wait 26µs (1250 ticks at 48MHz) for 38400 Hz sampling rate
                while (1) {
                    uint32_t current_val = SysTick->VAL;
                    uint32_t elapsed = (start_val >= current_val) ? (start_val - current_val) : (SysTick->LOAD - current_val + start_val);
                    if (elapsed >= 1250) break;
                }
                // Advance start_val by 1250 for isochronous sampling (SysTick counts down)
                start_val = (start_val >= 1250) ? (start_val - 1250) : (SysTick->LOAD - (1250 - start_val) + 1);

                // --- History Update (Strictly once per 38400 samples, precisely 1 second) ---
                if (++gSondeApp.history_sample_acc >= 38400) {
                    gSondeApp.history_sample_acc = 0;
                    gSondeApp.history[gSondeApp.history_ptr] = (uint8_t)gSondeApp.pending_history;
                    gSondeApp.history_ptr = (gSondeApp.history_ptr + 1) % 10;
                    gSondeApp.pending_history = '.'; // Reset for next second
                }
            } // End of bit-recovery loop

            // Restore ADC for battery measurement
            SARADC_CFG = (SARADC_CFG & ~SARADC_CFG_CH_SEL_MASK) | 
                         (((ADC_CH4 | ADC_CH9) << SARADC_CFG_CH_SEL_SHIFT) & SARADC_CFG_CH_SEL_MASK);
                         
            if (save_cooldown > 0) {
                save_cooldown--;
            }

            // Update timeout counters and latch successful decodes
            if (frame_decoded) {
                gSondeApp.timeout_frames = 0;
                // Mark success for history latch
                gSondeApp.pending_history = (gSondeApp.decoder.data.lat_1e6 != 0) ? '|' : '-';
                
                // Auto-switch to Monitor if we have valid coordinates and are currently looking at Diagnostics
                if (gSondeApp.mode == SONDE_MODE_DIAGNOSTIC && gSondeApp.decoder.data.valid) {
                    gSondeApp.mode = SONDE_MODE_MONITOR;
                }

                // Periodic save to EEPROM with 15-second cooldown
                if (gSondeApp.decoder.data.valid && gSondeApp.decoder.data.lat_1e6 != 0) {
                    if (save_cooldown == 0) {
                        SavedSonde_t saved_data;
                        saved_data.frequency = gSondeApp.frequency;
                        saved_data.lat_1e6 = gSondeApp.decoder.data.lat_1e6;
                        saved_data.lon_1e6 = gSondeApp.decoder.data.lon_1e6;
                        saved_data.alt_cm = gSondeApp.decoder.data.alt_cm;
                        EEPROM_WriteBuffer(0x1F70, &saved_data, 8);
                        EEPROM_WriteBuffer(0x1F78, ((uint8_t*)&saved_data) + 8, 8);
                        save_cooldown = 15;
                    }
                }
            } else {
                gSondeApp.timeout_frames++;
                
                // Auto-switch back to Diagnostics if signal is lost for 30 seconds
                if (gSondeApp.timeout_frames > 30 && gSondeApp.mode == SONDE_MODE_MONITOR) {
                    gSondeApp.mode = SONDE_MODE_DIAGNOSTIC;
                }
            }
                         
            // Calculate and filter Peak-to-Peak amplitude
            gSondeApp.last_adc_p2p = (local_max > local_min) ? (local_max - local_min) : 0;
            if (gSondeApp.p2p_avg == 0) {
                gSondeApp.p2p_avg = gSondeApp.last_adc_p2p;
            } else {
                gSondeApp.p2p_avg = (gSondeApp.p2p_avg * 7 + gSondeApp.last_adc_p2p) >> 3;
            }

            // --- Software AGC / Manual Gain ---
            if (++gSondeApp.agc_counter >= 10) {
                gSondeApp.agc_counter = 0;
                
                uint16_t reg_48 = BK4819_ReadRegister(0x48);
                uint8_t current_gain = reg_48 & 0x7F;
                bool changed = false;
                
                if (gSondeApp.gain_mode == 0) {
                    // Auto Gain Mode
                    if (gSondeApp.last_rssi > 500) {
                        // Target P2P range: 1200 - 2400
                        if (gSondeApp.p2p_avg < 1200 && current_gain < 120) {
                            current_gain += 2;
                            changed = true;
                        } else if (gSondeApp.p2p_avg > 2800 && current_gain > 5) {
                            current_gain -= 4;
                            changed = true;
                        }
                    } else {
                        // No signal? Reset to a safe baseline gain (60)
                        if (current_gain != 60) {
                            current_gain = 60;
                            changed = true;
                        }
                    }
                } else {
                    // Manual Gain Mode
                    // 1: LOW (20), 2: MID (60), 3: HIGH (100), 4: MAX (127)
                    uint8_t target_gain = (gSondeApp.gain_mode == 1) ? 20 :
                                          (gSondeApp.gain_mode == 2) ? 60 : 
                                          (gSondeApp.gain_mode == 3) ? 100 : 127;
                    if (current_gain != target_gain) {
                        current_gain = target_gain;
                        changed = true;
                    }
                }
                
                if (changed) {
                    reg_48 = (reg_48 & ~0x007F) | current_gain;
                    BK4819_WriteRegister(0x48, reg_48);
                }
            }

        } else {
            // QR Mode: Pause processing, just wait and render
            SYSTEM_DelayMs(100);
        }

        // Update display with latest metrics
        gSondeApp.last_rssi = BK4819_GetRSSI();
        Sonde_Render();
        BACKLIGHT_TurnOn();
    } // End of main loop

    // Save final radiosonde state to EEPROM on clean exit
    SavedSonde_t final_save;
    final_save.frequency = gSondeApp.frequency;
    
    // Read previous EEPROM data to preserve coordinates if we don't have live ones
    SavedSonde_t old_saved;
    EEPROM_ReadBuffer(0x1F70, &old_saved, sizeof(old_saved));
    
    if (gSondeApp.decoder.data.valid && gSondeApp.decoder.data.lat_1e6 != 0) {
        final_save.lat_1e6 = gSondeApp.decoder.data.lat_1e6;
        final_save.lon_1e6 = gSondeApp.decoder.data.lon_1e6;
        final_save.alt_cm = gSondeApp.decoder.data.alt_cm;
    } else {
        // Preserve old coordinates from EEPROM
        final_save.lat_1e6 = old_saved.lat_1e6;
        final_save.lon_1e6 = old_saved.lon_1e6;
        final_save.alt_cm = old_saved.alt_cm;
    }
    EEPROM_WriteBuffer(0x1F70, &final_save, 8);
    EEPROM_WriteBuffer(0x1F78, ((uint8_t*)&final_save) + 8, 8);

    // ============================================================
    // Cleanup and hardware restoration on exit
    // ============================================================
    
    AUDIO_AudioPathOff();
    
    // Restore ADC multiplexer
    SARADC_CFG = (SARADC_CFG & ~SARADC_CFG_CH_SEL_MASK) | 
                 (((ADC_CH4 | ADC_CH9) << SARADC_CFG_CH_SEL_SHIFT) & SARADC_CFG_CH_SEL_MASK);

    // Restore MCU Pin 9 - PA8 (UART1_RX)
    PORTCON_PORTA_SEL1 &= ~PORTCON_PORTA_SEL1_A8_MASK;
    PORTCON_PORTA_PU &= ~PORTCON_PORTA_PU_A8_MASK;
    PORTCON_PORTA_PD &= ~PORTCON_PORTA_PD_A8_MASK;
    
    // Reset BK4819 to normal mode
    BK4819_Init();
    RADIO_SetupRegisters(true);

    // Clear display and return to main UI seamlessly
    ST7565_FillScreen(0);
    GUI_SelectNextDisplay(DISPLAY_MAIN);
}
