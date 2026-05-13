/* Radiosonde Application Module for UV-K5
 * Self-contained main loop (like APP_RunSpectrum)
 * Handles BK4819 tuning, bit recovery simulation, and RS41 decoding
 */

#include "radiosonde.h"
#include "rs41.h"
#include "qrcodegen.h"

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
#include "../bsp/dp32g030/gpio.h"
#include "../bsp/dp32g030/portcon.h"
#include "../bsp/dp32g030/saradc.h"
#include "ARMCM0.h"

// Default RS41 frequency range (400.000 - 406.000 MHz)
// BK4819 uses freq/10 format
#define SONDE_FREQ_START   40000000   // 400.000 MHz
#define SONDE_FREQ_END     40600000   // 406.000 MHz
#define SONDE_FREQ_STEP      10000   // 100 kHz step for scanning
#define SONDE_FREQ_DEFAULT 40300000   // 403.000 MHz default

#define SONDE_SCAN_DWELL_MS   2000   // 2s per channel (RS41 transmits every 1s)
#define SONDE_RSSI_THRESHOLD  500    // RSSI threshold for signal detect
#define SONDE_TIMEOUT_FRAMES  300    // ~30 seconds at 10 fps

SondeApp_t gSondeApp;

// ============================================================
// Display helpers
// ============================================================

static void Sonde_DrawStatusBar(void)
{
    char str[22];

    uint32_t freq_mhz = gSondeApp.frequency / 100000;
    uint32_t freq_khz = (gSondeApp.frequency / 100) % 1000;

    const char *mode_str = "IDLE";
    if (gSondeApp.mode == SONDE_MODE_LISTEN) mode_str = "RX ";
    if (gSondeApp.mode == SONDE_MODE_SCAN)   mode_str = "SCN";

    // Line 0: Frequency and Mode
    sprintf(str, "FREQ:%3lu.%03lu %s", freq_mhz, freq_khz, mode_str);
    UI_PrintStringSmall(str, 0, 127, 0);

    // Line 1: F + E + OK
    sprintf(str, "F:%u minE:%u OK:%u",
            gSondeApp.decoder.frames_received,
            gSondeApp.decoder.min_errors,
            gSondeApp.decoder.frames_crc_ok);
    UI_PrintStringSmall(str, 0, 127, 1);
}

static void Sonde_DrawData(const RS41_Data_t *d)
{
    char str[22];

    if (!d->valid) return;

    // Line 2: Sonde ID
    sprintf(str, "ID: %.8s", d->sonde_id);
    UI_PrintStringSmall(str, 0, 127, 2);

    // Line 3: Latitude
    int32_t lat_deg = d->lat_1e6 / 1000000;
    int32_t lat_frac = d->lat_1e6 % 1000000;
    if (lat_frac < 0) lat_frac = -lat_frac;
    char lat_dir = (d->lat_1e6 >= 0) ? 'N' : 'S';
    sprintf(str, "Lat: %ld.%04ld %c", (long)lat_deg, (long)(lat_frac / 100), lat_dir);
    UI_PrintStringSmall(str, 0, 127, 3);

    // Line 4: Longitude
    int32_t lon_deg = d->lon_1e6 / 1000000;
    int32_t lon_frac = d->lon_1e6 % 1000000;
    if (lon_frac < 0) lon_frac = -lon_frac;
    char lon_dir = (d->lon_1e6 >= 0) ? 'E' : 'W';
    sprintf(str, "Lon: %ld.%04ld %c", (long)lon_deg, (long)(lon_frac / 100), lon_dir);
    UI_PrintStringSmall(str, 0, 127, 4);

    // Line 5: Altitude and Battery
    int32_t alt_m = d->alt_cm / 100;
    sprintf(str, "Alt: %ldm  %u.%uV", (long)alt_m, d->batt_mv / 1000, (d->batt_mv % 1000) / 100);
    UI_PrintStringSmall(str, 0, 127, 5);

    // Line 6: Satellites and Time
    sprintf(str, "Sat:%2u   %02u:%02u:%02u", 
            d->numSV, d->gps_hour, d->gps_min, d->gps_sec);
    UI_PrintStringSmall(str, 0, 127, 6);

    // Line 7: Speed
    int h_spd = d->vH_cm / 100;
    int h_spd_f = (d->vH_cm % 100) / 10;
    int v_spd = d->vV_cm / 100;
    int v_spd_f = (d->vV_cm % 100) / 10;
    if (v_spd_f < 0) v_spd_f = -v_spd_f;
    
    sprintf(str, "Hs:%d.%dm/s Vs:%d.%dm/s", h_spd, h_spd_f, v_spd, v_spd_f);
    UI_PrintStringSmall(str, 0, 127, 7);
}

static void Sonde_DrawPixel(int x, int y, bool black) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    if (black) {
        gFrameBuffer[y / 8][x] |= (1 << (y % 8));
    } else {
        gFrameBuffer[y / 8][x] &= ~(1 << (y % 8));
    }
}

static void Sonde_DrawQRCode(const RS41_Data_t *d) {
    if (!d->valid) {
        UI_PrintStringSmall("NO GPS DATA", 0, 127, 3);
        return;
    }

    char url[64];
    int32_t lat_deg = d->lat_1e6 / 1000000;
    int32_t lat_frac = d->lat_1e6 % 1000000;
    if (lat_frac < 0) lat_frac = -lat_frac;
    int32_t lon_deg = d->lon_1e6 / 1000000;
    int32_t lon_frac = d->lon_1e6 % 1000000;
    if (lon_frac < 0) lon_frac = -lon_frac;

    // geo:lat,lon
    sprintf(url, "geo:%ld.%04ld,%ld.%04ld", 
            (long)lat_deg, (long)(lat_frac / 100), 
            (long)lon_deg, (long)(lon_frac / 100));

    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];

    bool ok = qrcodegen_encodeText(url, tempBuffer, qrcode, qrcodegen_Ecc_LOW,
        qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true);

    if (ok) {
        int size = qrcodegen_getSize(qrcode);
        int scale = (size * 2 <= 60) ? 2 : 1; // 2x scale if fits, with some margin
        
        // Center the QR code
        int offset_x = (128 - size * scale) / 2;
        int offset_y = (64 - size * scale) / 2;

        // Draw white background
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 128; x++) {
                Sonde_DrawPixel(x, y, false);
            }
        }

        // Draw QR
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                bool isDark = qrcodegen_getModule(qrcode, x, y);
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        Sonde_DrawPixel(offset_x + x * scale + dx, offset_y + y * scale + dy, isDark);
                    }
                }
            }
        }
    }
}

static void Sonde_Render(void)
{
    UI_DisplayClear();
    if (gSondeApp.mode == SONDE_MODE_QR) {
        const RS41_Data_t *d = RS41_GetData(&gSondeApp.decoder);
        Sonde_DrawQRCode(d);
    } else {
        Sonde_DrawStatusBar();
        const RS41_Data_t *d = RS41_GetData(&gSondeApp.decoder);
        Sonde_DrawData(d);
    }
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
    reg_48 = (reg_48 & ~0x007F) | 127; // Max gain
    BK4819_WriteRegister(0x48, reg_48);

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
            // Exit radiosonde mode
            return true;

        case KEY_UP:
            // Increase frequency by step
            gSondeApp.frequency += SONDE_FREQ_STEP;
            if (gSondeApp.frequency > SONDE_FREQ_END)
                gSondeApp.frequency = SONDE_FREQ_START;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_DOWN:
            // Decrease frequency
            if (gSondeApp.frequency <= SONDE_FREQ_START)
                gSondeApp.frequency = SONDE_FREQ_END;
            else
                gSondeApp.frequency -= SONDE_FREQ_STEP;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_MENU:
            if (gSondeApp.mode == SONDE_MODE_LISTEN) {
                gSondeApp.mode = SONDE_MODE_SCAN;
                gSondeApp.frequency = SONDE_FREQ_START;
                Sonde_SetupReceiver(gSondeApp.frequency);
                RS41_Reset(&gSondeApp.decoder);
            } else if (gSondeApp.mode == SONDE_MODE_SCAN) {
                gSondeApp.mode = SONDE_MODE_QR;
            } else {
                gSondeApp.mode = SONDE_MODE_LISTEN;
            }
            break;

        case KEY_1:
            // Quick tune: 400 MHz
            gSondeApp.frequency = 40000000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_2:
            // Quick tune: 402 MHz
            gSondeApp.frequency = 40200000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_3:
            // Quick tune: 403 MHz (most common)
            gSondeApp.frequency = 40300000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_4:
            // Quick tune: 404 MHz
            gSondeApp.frequency = 40400000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        case KEY_5:
            // Quick tune: 405 MHz
            gSondeApp.frequency = 40500000;
            Sonde_SetupReceiver(gSondeApp.frequency);
            RS41_Reset(&gSondeApp.decoder);
            break;

        default:
            break;
    }

    return false;
}

// ============================================================
// Scan mode: step through frequencies looking for signal
// ============================================================

static void Sonde_ScanStep(void)
{
    uint16_t rssi = BK4819_GetRSSI();
    gSondeApp.last_rssi = rssi;

    if (rssi > SONDE_RSSI_THRESHOLD) {
        // Signal found, switch to listen mode
        gSondeApp.mode = SONDE_MODE_LISTEN;
        gSondeApp.signal_found = true;
        return;
    }

    // Move to next frequency
    gSondeApp.frequency += SONDE_FREQ_STEP;
    if (gSondeApp.frequency > SONDE_FREQ_END) {
        gSondeApp.frequency = SONDE_FREQ_START;
    }
    Sonde_SetupReceiver(gSondeApp.frequency);
}

// ============================================================
// Main application loop
// Takes over from main loop, similar to APP_RunSpectrum()
// ============================================================

void APP_RunRadiosonde(void)
{
    // Initialize
    memset(&gSondeApp, 0, sizeof(SondeApp_t));
    gSondeApp.mode = SONDE_MODE_LISTEN;
    gSondeApp.frequency = SONDE_FREQ_DEFAULT;
    gSondeApp.scan_freq_start = SONDE_FREQ_START;
    gSondeApp.scan_freq_end   = SONDE_FREQ_END;
    gSondeApp.scan_freq_step  = SONDE_FREQ_STEP;
    gSondeApp.scan_dwell_ms   = SONDE_SCAN_DWELL_MS;

    // ADC setup for signal monitoring (PA8 / UART1_RX -> ADC_CH3)
    PORTCON_PORTA_IE &= ~PORTCON_PORTA_IE_A8_MASK;
    PORTCON_PORTA_PU |= PORTCON_PORTA_PU_A8_MASK;   // Enable Pull-Up
    PORTCON_PORTA_PD |= PORTCON_PORTA_PD_A8_MASK;   // Enable Pull-Down to create ~VDD/2 bias
    PORTCON_PORTA_SEL1 &= ~PORTCON_PORTA_SEL1_A8_MASK;
    PORTCON_PORTA_SEL1 |= PORTCON_PORTA_SEL1_A8_BITS_SARADC_CH3;

    RS41_Init(&gSondeApp.decoder);

    // Enable backlight
    BACKLIGHT_TurnOn();

    // Setup receiver
    Sonde_SetupReceiver(gSondeApp.frequency);

    // Initial render
    Sonde_Render();

    uint16_t render_timer = 0;
    uint16_t sample_timer = 0;

    // Main loop — runs until user presses EXIT
    while (!gSondeApp.exit_requested) {

        // Handle keyboard
        if (Sonde_HandleKeys()) {
            gSondeApp.exit_requested = true;
            break;
        }

        // Scan mode: step through frequencies
        if (gSondeApp.mode == SONDE_MODE_SCAN) {
            gSondeApp.scan_timer++;
            if (gSondeApp.scan_timer >= (SONDE_SCAN_DWELL_MS / 10)) {
                gSondeApp.scan_timer = 0;
                Sonde_ScanStep();
            }
        }

        // Sample bits from FM demodulator
        // Using a Digital Phase Locked Loop (DPLL) for clock recovery
        // 4800 baud bit period is ~208 us. We sample 4x faster (52 us)
        if (gSondeApp.mode == SONDE_MODE_LISTEN) {

            // ADC sampling on PA8/ADC_CH3 (connected to BK4819 EARO via 100nF cap)
            SARADC_CFG = (SARADC_CFG & ~SARADC_CFG_CH_SEL_MASK) | 
                         ((ADC_CH3 << SARADC_CFG_CH_SEL_SHIFT) & SARADC_CFG_CH_SEL_MASK);

            uint32_t sample_count = 0;
            bool frame_decoded = false;
            uint16_t local_min = 0xFFFF;
            uint16_t local_max = 0;

            // Zero-crossing state
            int par_alt = 1;
            uint32_t samples_since_cross = 0;

            uint32_t start_val = SysTick->VAL;
            while (1) {
                sample_count++;
                
                // Read ADC
                ADC_Start();
                while (!ADC_CheckEndOfConversion(ADC_CH3)) {}
                uint16_t adc_val = ADC_GetValue(ADC_CH3);
                
                if (adc_val < local_min) local_min = adc_val;
                if (adc_val > local_max) local_max = adc_val;

                // Dynamic DC threshold (slow EMA, alpha = 1/256)
                gSondeApp.last_adc_raw = adc_val;
                if (gSondeApp.adc_avg_x512 == 0) gSondeApp.adc_avg_x512 = (uint32_t)adc_val << 9;
                gSondeApp.adc_avg_x512 = (gSondeApp.adc_avg_x512 * 511 + ((uint32_t)adc_val << 9)) >> 9;
                uint16_t threshold = gSondeApp.adc_avg_x512 >> 9;
                
                // Remove DC offset
                int32_t centered = (int32_t)adc_val - (int32_t)threshold;
                gSondeApp.last_amplitude = (uint16_t)((centered > 0) ? centered : -centered);

                // Hysteresis for Zero-crossing:
                // Only consider it a valid signal if amplitude is somewhat reasonable
                int current_par = (centered > 100) ? 1 : ((centered < -100) ? -1 : par_alt);


                if (current_par * par_alt <= 0) { // Zero crossing occurred
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

            }

            // Restore ADC for battery measurement
            SARADC_CFG = (SARADC_CFG & ~SARADC_CFG_CH_SEL_MASK) | 
                         (((ADC_CH4 | ADC_CH9) << SARADC_CFG_CH_SEL_SHIFT) & SARADC_CFG_CH_SEL_MASK);
                         
            // Save p2p for UI
            gSondeApp.last_adc_p2p = (local_max > local_min) ? (local_max - local_min) : 0;

        } else {
            SYSTEM_DelayMs(1);
        }

        // Update display on every cycle (since the DPLL loop takes 200ms to 1000ms anyway)
        gSondeApp.last_rssi = BK4819_GetRSSI();
        Sonde_Render();
        BACKLIGHT_TurnOn();
    }

    // Cleanup: restore normal radio operation
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);

    // Reconfigure VFOs for normal operation
    RADIO_SetupRegisters(true);
}
