/* Radiosonde Application Module for UV-K5
 * Provides the main application loop for RS41 decoding mode
 */

#ifndef APP_RADIOSONDE_H
#define APP_RADIOSONDE_H

#include <stdint.h>
#include <stdbool.h>
#include "rs41.h"

// Radiosonde app states
typedef enum {
    SONDE_MODE_IDLE,
    SONDE_MODE_LISTEN,
    SONDE_MODE_QR,
} SondeMode_t;

// Application context
typedef struct {
    SondeMode_t mode;
    RS41_Decoder_t decoder;
    uint32_t frequency;        // current frequency in Hz/10 (BK4819 format)

    uint16_t last_rssi;        // RSSI of current channel
    uint16_t last_adc_p2p;     // ADC peak-to-peak amplitude
    bool     signal_found;     // signal detected on current freq
    bool     exit_requested;   // user wants to exit
    uint16_t timeout_frames;   // frames without decode before timeout
    uint16_t last_adc_raw;     // raw ADC sample for debug
    uint32_t adc_avg_x512;     // DC average (accumulated)
    uint16_t last_amplitude;   // Peak amplitude after DC removal
} SondeApp_t;

extern SondeApp_t gSondeApp;

// Main entry point — takes over the main loop (similar to APP_RunSpectrum)
void APP_RunRadiosonde(void);

#endif // APP_RADIOSONDE_H
