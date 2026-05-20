// app/cw_decoder.c
// CW (Morse Code) decoder using BK4819 Voice Amplitude register (REG_64)
// Algorithm:
//   1. Poll BK4819_GetVoiceAmplitudeOut() every 10ms
//   2. Adaptive threshold: EMA of amplitude → detect Mark/Space
//   3. Measure Mark/Space durations → classify dit/dah/gap/word-gap
//   4. Binary tree lookup → ASCII character
//   5. Display on screen via GUI_DisplaySmallest

#include "app/cw_decoder.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "ui/helper.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Morse binary tree lookup table (64 entries, index starting at 1)
// Navigation: dot → index*2, dash → index*2+1
// ---------------------------------------------------------------------------
static const char MORSE_TABLE[64] = {
    0,   0,   'E', 'T', 'I', 'A', 'N', 'M',
    'S', 'U', 'R', 'W', 'D', 'K', 'G', 'O',
    'H', 'V', 'F', 0,   'L', 0,   'P', 'J',
    'B', 'X', 'C', 'Y', 'Z', 'Q', 0,   0,
    '5', '4', 0,   '3', 0,   0,   0,   '2',
    0,   0,   0,   0,   0,   0,   0,   '1',
    '6', '=', '/', 0,   0,   0,   '(', 0,
    '7', 0,   '8', 0,   '9', '0', 0,   0
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static uint8_t  cw_buf[CW_DISPLAY_LEN + 1];  // decoded text ring buffer
static uint8_t  cw_pos;                        // write position in buffer

static uint16_t cw_avg;       // EMA of amplitude (scaled x8 to avoid floats)
static uint16_t cw_dit;       // estimated dit length in ticks (10ms each)

static bool     cw_mark;      // current state: true = tone present
static uint16_t cw_dur;       // duration of current mark or space (ticks)
static uint8_t  cw_tree;      // current position in morse binary tree (1=root)

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static void cw_push_char(char c) {
    if (c == 0) return;
    if (cw_pos >= CW_DISPLAY_LEN) {
        // scroll left
        memmove(&cw_buf[0], &cw_buf[1], CW_DISPLAY_LEN - 1);
        cw_pos = CW_DISPLAY_LEN - 1;
    }
    cw_buf[cw_pos++] = (uint8_t)c;
    cw_buf[cw_pos]   = 0;
}

static void cw_decode_char(void) {
    if (cw_tree >= 1 && cw_tree < 64) {
        cw_push_char(MORSE_TABLE[cw_tree]);
    }
    cw_tree = 1;   // reset to root
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void CW_Init(void) {
    memset(cw_buf, ' ', CW_DISPLAY_LEN);
    cw_buf[CW_DISPLAY_LEN] = 0;
    cw_pos   = 0;
    cw_avg   = CW_MIN_THRESH * 8;
    cw_dit   = 8;   // initial guess: 8 ticks = 80ms = ~15 WPM
    cw_mark  = false;
    cw_dur   = 0;
    cw_tree  = 1;
}

// Call this every 10ms from the main scheduler / SI_key loop
void CW_Tick(void) {
    // 1. Read amplitude from BK4819 REG_64 (0..32767)
    uint16_t amp = BK4819_GetVoiceAmplitudeOut();

    // 2. Update slow EMA (alpha = 1/8)
    cw_avg = (uint16_t)((cw_avg * 7u + (uint32_t)amp * 8u) >> 3);
    uint16_t threshold = (cw_avg >> 3);   // = cw_avg/8
    if (threshold < CW_MIN_THRESH) threshold = CW_MIN_THRESH;

    // 3. Detect current state
    bool is_mark = (amp > threshold);

    if (is_mark == cw_mark) {
        // Same state: just count ticks, cap at 255 to save RAM
        if (cw_dur < 255) cw_dur++;
        return;
    }

    // State transition
    if (cw_mark) {
        // Mark just ended → classify dit or dah
        if (cw_tree < 32) {   // guard: prevent overflow
            if (cw_dur <= cw_dit + (cw_dit >> 1)) {
                // dit: go left in tree (x2)
                cw_tree = (uint8_t)(cw_tree * 2);
                // Update dit length estimate (EMA alpha=1/4)
                cw_dit = (uint16_t)((cw_dit * 3u + cw_dur) >> 2);
            } else {
                // dah: go right in tree (x2+1)
                cw_tree = (uint8_t)(cw_tree * 2 + 1);
                // Update dit length from dah/3
                cw_dit = (uint16_t)((cw_dit * 3u + cw_dur / 3u) >> 2);
            }
        }
    } else {
        // Space just ended → classify gap
        if (cw_dur >= cw_dit * 7u) {
            // Word gap → decode char + push space
            cw_decode_char();
            cw_push_char(' ');
        } else if (cw_dur >= cw_dit * 2u) {
            // Character gap → decode char
            cw_decode_char();
        }
        // else: inter-element gap, ignore
    }

    cw_mark = is_mark;
    cw_dur  = 0;
}

void CW_Display(void) {
    // Show decoded text on bottom row of LCD
    GUI_DisplaySmallest((char *)cw_buf, 0, 56, false, true);
    ST7565_BlitFullScreen();
}
