// app/cw.c
// CW (Morse Code) TX + RX for Quansheng UV-K5 — no hardware mod required
//
// RX:  polls BK4819 Voice Amplitude (REG_64) every 10ms, adaptive threshold,
//      auto-WPM estimation, Morse binary tree decode → displays decoded text
// TX paddle: Side Key 1 = dit, Side Key 2 = dah (one element at a time)
//            UP / DOWN adjust WPM (5-40)
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
// TX element queue  (positive ticks = tone ON, negative = tone OFF)
// ============================================================
#define TX_Q_LEN  8   // enough for one dit or dah + gap

static int8_t  tx_q[TX_Q_LEN];
static uint8_t tx_head;
static int8_t  tx_cnt;       // countdown for current element
static bool    tx_active;    // true while RF is keyed

// ============================================================
// RX decoder state
// ============================================================
static char     rx_buf[CW_DISPLAY_LEN + 1];
static uint8_t  rx_pos;
static uint16_t rx_avg8;     // amplitude EMA × 8
static uint16_t rx_dit;      // estimated dit length (ticks)
static bool     rx_mark;     // true = tone present
static uint16_t rx_dur;      // ticks in current state
static uint8_t  rx_tree;     // binary tree position

// ============================================================
// WPM
// ============================================================
static uint8_t cw_wpm = CW_WPM_DEFAULT;

// dit_ticks = 1200ms / wpm / 10ms = 120 / wpm
#define DIT_T(wpm) ((uint8_t)(120u / (wpm)))

// ============================================================
// TX helpers (static, inlined by LTO)
// ============================================================

static void tx_on(void) {
    if (tx_active) return;
    tx_active = true;
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    FUNCTION_Select(FUNCTION_TRANSMIT);
    RADIO_SetTxParameters();
    BK4819_TransmitTone(true, CW_TONE_HZ);  // local loopback = sidetone
    AUDIO_AudioPathOn();
    gEnableSpeaker = true;
    BK4819_EnterTxMute();   // start muted; first element unmutes
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

// Queue one dit or dah: tone + inter-element gap
static void q_element(bool is_dah) {
    uint8_t dit = DIT_T(cw_wpm);
    uint8_t tone = (uint8_t)(is_dah ? dit * 3u : dit);
    if (tone > 127u) tone = 127u;
    if (dit  > 127u) dit  = 127u;
    tx_q[0] = (int8_t)tone;   // tone
    tx_q[1] = -(int8_t)dit;   // inter-element gap
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
    rx_pos   = 0;
    rx_avg8  = CW_MIN_THRESH * 8u;
    rx_dit   = 8u;
    rx_mark  = false;
    rx_dur   = 0;
    rx_tree  = 1u;

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
        return;   // don't run RX while TX is active
    } else if (tx_active) {
        tx_off();
        return;
    }

    // --- RX decoder (polling amplitude every 10ms) ---
    uint16_t amp = BK4819_GetVoiceAmplitudeOut();

    // EMA alpha = 1/8
    rx_avg8 = (uint16_t)((rx_avg8 * 7u + (uint32_t)amp * 8u) >> 3u);
    uint16_t thr = rx_avg8 >> 3u;
    if (thr < CW_MIN_THRESH) thr = CW_MIN_THRESH;

    bool is_mark = (amp > thr);
    if (is_mark == rx_mark) {
        if (rx_dur < 255u) rx_dur++;
        return;
    }

    if (rx_mark) {
        // Mark ended → classify dit or dah
        if (rx_tree < 32u) {
            if (rx_dur <= rx_dit + (rx_dit >> 1u))
                rx_tree = (uint8_t)(rx_tree * 2u);        // dit
            else
                rx_tree = (uint8_t)(rx_tree * 2u + 1u);   // dah
            rx_dit = (uint16_t)((rx_dit * 3u + rx_dur) >> 2u);
        }
    } else {
        // Space ended → classify gap
        if      (rx_dur >= rx_dit * 7u) { rx_commit(); rx_push(' '); }
        else if (rx_dur >= rx_dit * 2u)   rx_commit();
    }

    rx_mark = is_mark;
    rx_dur  = 0;
}

// Called from MAIN_ProcessKeys when gCW_Active
void CW_ProcessKey(KEY_Code_t Key, bool pressed, bool held) {
    switch (Key) {

        case KEY_SIDE1:  // dit
            if (pressed && !held && !tx_active)
                q_element(false);
            break;

        case KEY_SIDE2:  // dah
            if (pressed && !held && !tx_active)
                q_element(true);
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
            if (!pressed && !held)
                CW_Deinit();
            break;

        default:
            break;
    }
}

// Render CW screen (called from GUI_DisplayScreen when gCW_Active)
void CW_Display(void) {
    static const char header[] = "-CW- SK1=dit SK2=dah";
    char wpm_str[6];

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

    // Row 0: header
    UI_PrintStringSmall(header, 0, 127, 0);

    // Row 1: WPM + TX/RX status
    wpm_str[0] = '0' + (uint8_t)(cw_wpm / 10u);
    wpm_str[1] = '0' + (uint8_t)(cw_wpm % 10u);
    wpm_str[2] = 'W';
    wpm_str[3] = 'P';
    wpm_str[4] = 'M';
    wpm_str[5] = 0;
    UI_PrintStringSmall(wpm_str, 0, 40, 2);
    UI_PrintStringSmall(tx_active ? " TX" : " RX", 40, 80, 2);

    // Row 2: decoded RX text
    UI_PrintStringSmall(rx_buf, 0, 127, 4);

    ST7565_BlitFullScreen();
}
