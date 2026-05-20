// app/cw.c
// CW (Morse Code) TX + RX for Quansheng UV-K5 — no hardware mod required
//
// RX: polls BK4819 RSSI (REG_67) every 10ms — works in FM mode!
//     A CW carrier raises RSSI when ON, drops when OFF.
//     Hysteresis + noise-floor tracking for reliable mark/space detection.
//     Auto WPM estimation via mark duration EMA.
//
// TX paddle: Side Key 1 = dit, Side Key 2 = dah
//            UP / DOWN adjust WPM (5-40)
//
// Toggle: Long Press 9
// Exit:   EXIT key while CW mode is active

#include "app/cw.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "ui/helper.h"
#include "misc.h"
#include "audio.h"
#include "functions.h"
#include "radio.h"
#include "app/app.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Public state
// ============================================================
bool gCW_Active = false;

// ============================================================
// Morse RX binary tree (64 entries, index 1 = root)
// Navigate: dit → index*2, dah → index*2+1
// ============================================================
static const char CW_RX_TABLE[64] = {
    0,   0,   'E', 'T', 'I', 'A', 'N', 'M',
    'S', 'U', 'R', 'W', 'D', 'K', 'G', 'O',
    'H', 'V', 'F', 0,   'L', 0,   'P', 'J',
    'B', 'X', 'C', 'Y', 'Z', 'Q', 0,   0,
    '5', '4', 0,   '3', 0,   0,   0,   '2',
    0,   0,   0,   0,   0,   0,   0,   '1',
    '6', '=', '/', 0,   0,   0,   '(', 0,
    '7', 0,   '8', 0,   '9', '0', 0,   0
};

// ============================================================
// TX element queue (positive ticks = tone ON, negative = silence)
// ============================================================
#define TX_Q_LEN 8

static int8_t  tx_q[TX_Q_LEN];
static uint8_t tx_head;
static int8_t  tx_cnt;
static bool    tx_active;

// ============================================================
// RX decoder state
// ============================================================
static char     rx_buf[CW_DISPLAY_LEN + 1];
static uint8_t  rx_pos;

// Noise floor tracker (updated only during silence = carrier OFF)
// Units: raw RSSI (REG_67[8:0]), range 0..511
static uint16_t rx_noise8;      // noise floor EMA × 8
static uint16_t rx_signal8;     // peak signal EMA × 8 (updated during mark)

static bool     rx_mark;        // current state: mark or space
static uint16_t rx_dur;         // ticks in current state
static uint8_t  rx_tree;        // binary tree position
static uint16_t rx_dit;         // estimated dit length in ticks

// Calibration counter: first 30 ticks = learn noise floor, no decode
static uint8_t  rx_calib;

// ============================================================
// WPM
// ============================================================
static uint8_t cw_wpm = CW_WPM_DEFAULT;

#define DIT_T(wpm) ((uint8_t)(120u / (wpm)))

// ============================================================
// TX helpers
// ============================================================

static void tx_on(void) {
    if (tx_active) return;
    tx_active = true;
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    FUNCTION_Select(FUNCTION_TRANSMIT);
    RADIO_SetTxParameters();
    BK4819_TransmitTone(true, CW_TONE_HZ);
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
    BK4819_EnterTxMute();
}

static void tx_off(void) {
    if (!tx_active) return;
    BK4819_EnterTxMute();
    APP_EndTransmission(true);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    gEnableSpeaker = false;
    tx_active = false;
    tx_head   = 0;
    tx_cnt    = 0;
    tx_q[0]   = 0;
}

static void q_element(bool is_dah) {
    uint8_t dit  = DIT_T(cw_wpm);
    uint8_t tone = (uint8_t)(is_dah ? dit * 3u : dit);
    if (tone > 127u) tone = 127u;
    if (dit  > 127u) dit  = 127u;
    tx_q[0] = (int8_t)tone;
    tx_q[1] = -(int8_t)dit;
    tx_q[2] = 0;
    tx_head  = 0;
}

// ============================================================
// RX helpers
// ============================================================

static void rx_push(char c) {
    if (!c) return;
    if (rx_pos >= CW_DISPLAY_LEN) {
        memmove(&rx_buf[0], &rx_buf[1], CW_DISPLAY_LEN - 1u);
        rx_pos = CW_DISPLAY_LEN - 1u;
    }
    rx_buf[rx_pos++] = c;
    rx_buf[rx_pos]   = 0;
}

static void rx_commit(void) {
    if (rx_tree >= 1u && rx_tree < 64u)
        rx_push(CW_RX_TABLE[rx_tree]);
    rx_tree = 1u;
}

// ============================================================
// Public API
// ============================================================

void CW_Init(void) {
    memset(rx_buf, ' ', CW_DISPLAY_LEN);
    rx_buf[CW_DISPLAY_LEN] = 0;
    rx_pos    = 0;
    rx_noise8 = CW_RSSI_NOISE_INIT * 8u;
    rx_signal8 = CW_RSSI_NOISE_INIT * 8u;
    rx_dit    = 8u;
    rx_mark   = false;
    rx_dur    = 0;
    rx_tree   = 1u;
    rx_calib  = 30u;   // 300ms calibration

    tx_active = false;
    tx_head   = 0;
    tx_cnt    = 0;
    tx_q[0]   = 0;

    gCW_Active = true;
}

void CW_Deinit(void) {
    if (tx_active) tx_off();
    gCW_Active = false;
}

// Called every 10ms from APP_TimeSlice10ms
void CW_Tick(void) {
    if (!gCW_Active) return;

    // --- TX state machine ---
    if (tx_q[tx_head] != 0) {
        if (!tx_active) tx_on();
        if (tx_cnt <= 0) {
            int8_t elem = tx_q[tx_head];
            if (elem == 0) {
                tx_off();
            } else {
                tx_cnt = (elem < 0) ? -elem : elem;
                if (elem > 0) BK4819_ExitTxMute();
                else          BK4819_EnterTxMute();
                tx_head++;
            }
        } else {
            tx_cnt--;
        }
        return;
    } else if (tx_active) {
        tx_off();
        return;
    }

    // --- RX: read RSSI (works in FM mode — CW raises carrier RSSI) ---
    uint16_t rssi = BK4819_GetRSSI();   // 0..511

    // Calibration: learn noise floor first, don't decode
    if (rx_calib > 0) {
        rx_calib--;
        // Fast init of noise floor
        rx_noise8 = (uint16_t)((rx_noise8 * 7u + (uint32_t)rssi * 8u) >> 3u);
        return;
    }

    // --- Adaptive thresholds with hysteresis ---
    // Noise floor: updated ONLY when in silence (space state)
    if (!rx_mark) {
        // Slow EMA: alpha = 1/16
        rx_noise8 = (uint16_t)((rx_noise8 * 15u + (uint32_t)rssi * 8u) >> 4u);
    } else {
        // Signal peak: updated during mark
        rx_signal8 = (uint16_t)((rx_signal8 * 7u + (uint32_t)rssi * 8u) >> 3u);
    }

    uint16_t noise   = rx_noise8  >> 3u;   // noise floor
    uint16_t signal  = rx_signal8 >> 3u;   // signal level
    uint16_t midpoint = (uint16_t)((noise + signal) >> 1u);

    // Guard: need meaningful signal above noise
    if (midpoint < noise + CW_RSSI_MIN_DELTA) {
        // Signal too weak — reset state, wait
        rx_mark = false;
        rx_dur  = 0;
        rx_tree = 1u;
        return;
    }

    // Hysteresis: start mark at 75% of midpoint above noise,
    //             end mark at 25% of midpoint above noise
    uint16_t hi_thr = (uint16_t)(noise + ((midpoint - noise) * 3u >> 2u));
    uint16_t lo_thr = (uint16_t)(noise + ((midpoint - noise) >> 2u));

    bool is_mark;
    if (rx_mark)
        is_mark = (rssi > lo_thr);   // keep mark until signal drops below lo
    else
        is_mark = (rssi > hi_thr);   // start mark only when signal exceeds hi

    if (is_mark == rx_mark) {
        if (rx_dur < 255u) rx_dur++;
        return;
    }

    // --- State transition ---
    if (rx_mark) {
        // Mark just ended → classify dit or dah
        if (rx_tree < 32u) {
            uint16_t boundary = (uint16_t)(rx_dit + (rx_dit >> 1u));
            if (rx_dur <= boundary)
                rx_tree = (uint8_t)(rx_tree * 2u);         // dit
            else
                rx_tree = (uint8_t)(rx_tree * 2u + 1u);    // dah

            // Update dit estimate (EMA alpha = 1/4) — clamp to sane range
            uint16_t new_dit = (uint16_t)((rx_dit * 3u + rx_dur) >> 2u);
            if (new_dit > 1u && new_dit < 30u)
                rx_dit = new_dit;
        }
    } else {
        // Space just ended → classify gap
        if      (rx_dur >= rx_dit * 7u) { rx_commit(); rx_push(' '); }
        else if (rx_dur >= rx_dit * 2u)   rx_commit();
    }

    rx_mark = is_mark;
    rx_dur  = 0;
}

// Called from MAIN_ProcessKeys when gCW_Active
void CW_ProcessKey(KEY_Code_t Key, bool pressed, bool held) {
    switch (Key) {
        case KEY_SIDE1:
            if (pressed && !held && !tx_active) q_element(false);
            break;
        case KEY_SIDE2:
            if (pressed && !held && !tx_active) q_element(true);
            break;
        case KEY_UP:
            if (!pressed && !held && cw_wpm < CW_WPM_MAX)
                cw_wpm = (uint8_t)(cw_wpm + 5u);
            break;
        case KEY_DOWN:
            if (!pressed && !held && cw_wpm > CW_WPM_MIN)
                cw_wpm = (uint8_t)(cw_wpm - 5u);
            break;
        case KEY_EXIT:
            if (!pressed && !held) CW_Deinit();
            break;
        default:
            break;
    }
}

// Render CW screen
void CW_Display(void) {
    char wpm_str[20];

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

    // Row 0: header (shortened to prevent overflow crash)
    UI_PrintStringSmall("-CW- SK1=. SK2=-", 0, 127, 0);

    // Row 2: WPM + status
    uint8_t n  = rx_noise8 >> 3u;
    uint8_t s  = rx_signal8 >> 3u;
    wpm_str[0]  = (char)('0' + cw_wpm / 10u);
    wpm_str[1]  = (char)('0' + cw_wpm % 10u);
    wpm_str[2]  = 'W';
    wpm_str[3]  = ' ';
    wpm_str[4]  = tx_active ? 'T' : (rx_calib ? 'C' : (rx_mark ? '*' : ' '));
    wpm_str[5]  = 'X';
    wpm_str[6]  = ' ';
    wpm_str[7]  = 'N';
    wpm_str[8]  = (char)('0' + (n / 100u) % 10u);
    wpm_str[9]  = (char)('0' + (n / 10u)  % 10u);
    wpm_str[10] = (char)('0' + n % 10u);
    wpm_str[11] = ' ';
    wpm_str[12] = 'S';
    wpm_str[13] = (char)('0' + (s / 100u) % 10u);
    wpm_str[14] = (char)('0' + (s / 10u)  % 10u);
    wpm_str[15] = (char)('0' + s % 10u);
    wpm_str[16] = 0;
    UI_PrintStringSmall(wpm_str, 0, 127, 2);

    // Row 4: decoded text
    UI_PrintStringSmall(rx_buf, 0, 127, 4);

    // Row 6: UP/DN=WPM EXIT=quit (split into 2 columns to prevent centering overflow crash)
    UI_PrintStringSmall("UP/DN=WPM", 0, 63, 6);
    UI_PrintStringSmall("EXIT=QUIT", 64, 127, 6);

    ST7565_BlitFullScreen();
}
