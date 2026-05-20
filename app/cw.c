// app/cw.c
// CW (Morse Code) TX + RX for Quansheng UV-K5 — no hardware mod required
//
// RX: polls BK4819 RSSI (REG_67) every 10ms — works in FM mode!
//     A CW carrier raises RSSI when ON, drops when OFF.
//     Hysteresis + noise-floor tracking for reliable mark/space detection.
//     Auto WPM estimation via mark duration EMA.
//
// TX paddles: Side Key 1 = dit, Side Key 2 = dah
// TX keyboard: Keys 0-9 = multi-tap text entry, Star = Backspace, Menu = Send text
//              F = Toggle sound output (mute/unmute speaker)
//              UP / DOWN adjust WPM (5-40)
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

// Keypad multi-tap mapping (flattened to save flash)
static const char KEY_MAP_STR[] = " 0,.1ABC2DEF3GHI4JKL5MNO6PQRS7TUV8WXYZ9";
static const uint8_t KEY_MAP_OFF[] = {0, 2, 5, 9, 13, 17, 21, 25, 30, 34, 39};

// ============================================================
// TX element queue (positive ticks = tone ON, negative = silence)
// ============================================================
#define TX_Q_LEN 8

static int8_t  tx_q[TX_Q_LEN];
static uint8_t tx_head;
static int8_t  tx_cnt;
static bool    tx_active;

// Speaker / Sound output state (mute/unmute local speaker during RX/TX)
static bool        cw_speaker;

// String transmission state
static const char* tx_str;
static uint8_t     tx_str_pos;
static uint8_t     tx_char_bits;
static uint8_t     tx_char_len;

// Multi-tap typing state
static KEY_Code_t  last_key;
static uint8_t     key_idx;
static uint16_t    key_timeout_ticks;

// ============================================================
// RX decoder state
// ============================================================
static char     rx_buf[CW_DISPLAY_LEN + 1];
static uint8_t  rx_pos;

// Noise floor tracker (updated only during silence = carrier OFF)
static uint16_t rx_noise8;      // noise floor EMA × 8
static uint16_t rx_signal8;     // peak signal EMA × 8 (updated during mark)

static bool     rx_mark;        // current state: mark or space
static uint16_t rx_dur;         // ticks in current state
static uint8_t  rx_tree;        // binary tree position
static uint16_t rx_dit;         // estimated dit length in ticks
static uint8_t  rx_calib;       // calibration counter

// ============================================================
// WPM Maps to avoid divisions
// ============================================================
static const uint8_t WPM_DIT_MAP[8] = { 24, 12, 8, 6, 5, 4, 3, 3 };
static const char WPM_NAMES[8][3] = { "05", "10", "15", "20", "25", "30", "35", "40" };
static uint8_t cw_wpm_idx = 2; // Default 15 WPM (index 2)
static uint8_t cw_dit_ticks;

// ============================================================
// TX helpers
// ============================================================

static void tx_on(void) {
    if (tx_active) return;
    tx_active = true;
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    FUNCTION_Select(FUNCTION_TRANSMIT);
    RADIO_SetTxParameters();
    
    // Enable local sidetone by enabling AF DAC in Reg 30
    if (cw_speaker) {
        BK4819_WriteRegister(BK4819_REG_30, 0xC3FE);
    } else {
        BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
    }

    BK4819_TransmitTone(true, CW_TONE_HZ);
    if (cw_speaker) {
        AUDIO_AudioPathOn();
        gEnableSpeaker = true;
    } else {
        gEnableSpeaker = false;
        AUDIO_AudioPathOff();
    }
    BK4819_EnterTxMute();
}

static void tx_off(void) {
    if (!tx_active) return;
    BK4819_EnterTxMute();
    BK4819_WriteRegister(BK4819_REG_70, 0); // Disable Tone 1 generator
    APP_EndTransmission(true);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    gEnableSpeaker = false;
    tx_active = false;
    tx_head   = 0;
    tx_cnt    = 0;
    tx_q[0]   = 0;
}

static void q_element(bool is_dah) {
    uint8_t dit  = cw_dit_ticks;
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
    rx_calib  = 30u;

    tx_active = false;
    tx_head   = 0;
    tx_cnt    = 0;
    tx_q[0]   = 0;

    cw_speaker = true;

    tx_str      = NULL;
    tx_str_pos  = 0;
    tx_char_len = 0;

    last_key          = KEY_INVALID;
    key_idx           = 0;
    key_timeout_ticks = 0;

    cw_dit_ticks = WPM_DIT_MAP[cw_wpm_idx];

    gCW_Active = true;
}

void CW_Deinit(void) {
    if (tx_active || tx_str != NULL) tx_off();
    gCW_Active = false;
    gEnableSpeaker = false;
    AUDIO_AudioPathOff();
}

// Called every 10ms from APP_TimeSlice10ms
void CW_Tick(void) {
    if (!gCW_Active) return;

    if (key_timeout_ticks > 0) {
        key_timeout_ticks--;
    }

    // --- TX string queue logic ---
    if (tx_q[tx_head] == 0 && tx_str != NULL) {
        if (tx_char_len == 0) {
            char c = tx_str[tx_str_pos];
            if (c == 0) {
                tx_str = NULL;
            } else {
                tx_str_pos++;
                if (c == ' ') {
                    uint8_t dit = cw_dit_ticks;
                    tx_q[0] = -(int8_t)(dit * 4u); // 4 more dits of silence (7 total)
                    tx_q[1] = 0;
                    tx_head = 0;
                    tx_cnt  = 0;
                } else {
                    if (c >= 'a' && c <= 'z') c -= 32;
                    uint8_t idx = 0;
                    for (uint8_t i = 2; i < 64; i++) {
                        if (CW_RX_TABLE[i] == c) {
                            idx = i;
                            break;
                        }
                    }
                    if (idx > 1) {
                        tx_char_bits = 0;
                        tx_char_len  = 0;
                        while (idx > 1) {
                            tx_char_bits = (uint8_t)((tx_char_bits << 1) | (idx & 1));
                            tx_char_len++;
                            if (idx & 1) idx = (uint8_t)((idx - 1) >> 1);
                            else         idx = (uint8_t)(idx >> 1);
                        }
                    }
                }
            }
        }

        if (tx_char_len > 0) {
            bool is_dah = (tx_char_bits & 1);
            tx_char_bits >>= 1;
            tx_char_len--;

            uint8_t dit = cw_dit_ticks;
            uint8_t tone = (uint8_t)(is_dah ? dit * 3u : dit);
            if (tone > 127u) tone = 127u;
            
            tx_q[0] = (int8_t)tone;
            if (tx_char_len == 0) {
                tx_q[1] = -(int8_t)(dit * 3u); // inter-character space (3 dits)
            } else {
                tx_q[1] = -(int8_t)dit;        // inter-element space (1 dit)
            }
            tx_q[2] = 0;
            tx_head = 0;
            tx_cnt  = 0;
        }
    }

    // --- TX state machine ---
    if (tx_q[tx_head] != 0) {
        if (!tx_active) tx_on();
        if (tx_cnt <= 0) {
            int8_t elem = tx_q[tx_head];
            if (elem == 0) {
                tx_off();
            } else {
                tx_cnt = (elem < 0) ? -elem : elem;
                if (elem > 0) {
                    BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_MASK_ENABLE_TONE1 | (66u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
                    BK4819_ExitTxMute();
                } else {
                    BK4819_EnterTxMute();
                    BK4819_WriteRegister(BK4819_REG_70, 0);
                }
                
                // Force speaker state during TX sidetone to avoid clicky pops
                if (cw_speaker) {
                    gEnableSpeaker = true;
                    AUDIO_AudioPathOn();
                } else {
                    gEnableSpeaker = false;
                    AUDIO_AudioPathOff();
                }
                
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

    // Force speaker state during RX
    if (cw_speaker) {
        gEnableSpeaker = true;
        AUDIO_AudioPathOn();
    } else {
        gEnableSpeaker = false;
        AUDIO_AudioPathOff();
    }

    // --- RX: read RSSI (works in FM mode — CW raises carrier RSSI) ---
    uint16_t rssi = BK4819_GetRSSI();

    if (rx_calib > 0) {
        rx_calib--;
        rx_noise8 = (uint16_t)((rx_noise8 * 7u + (uint32_t)rssi * 8u) >> 3u);
        return;
    }

    if (!rx_mark) {
        rx_noise8 = (uint16_t)((rx_noise8 * 15u + (uint32_t)rssi * 8u) >> 4u);
    } else {
        rx_signal8 = (uint16_t)((rx_signal8 * 7u + (uint32_t)rssi * 8u) >> 3u);
    }

    uint16_t noise   = rx_noise8  >> 3u;
    uint16_t signal  = rx_signal8 >> 3u;
    uint16_t midpoint = (uint16_t)((noise + signal) >> 1u);

    if (midpoint < noise + CW_RSSI_MIN_DELTA) {
        rx_mark = false;
        rx_dur  = 0;
        rx_tree = 1u;
        return;
    }

    uint16_t hi_thr = (uint16_t)(noise + ((midpoint - noise) * 3u >> 2u));
    uint16_t lo_thr = (uint16_t)(noise + ((midpoint - noise) >> 2u));

    bool is_mark;
    if (rx_mark)
        is_mark = (rssi > lo_thr);
    else
        is_mark = (rssi > hi_thr);

    if (is_mark == rx_mark) {
        if (rx_dur < 255u) rx_dur++;
        return;
    }

    if (rx_mark) {
        if (rx_tree < 32u) {
            uint16_t boundary = (uint16_t)(rx_dit + (rx_dit >> 1u));
            if (rx_dur <= boundary)
                rx_tree = (uint8_t)(rx_tree * 2u);
            else
                rx_tree = (uint8_t)(rx_tree * 2u + 1u);

            uint16_t new_dit = (uint16_t)((rx_dit * 3u + rx_dur) >> 2u);
            if (new_dit > 1u && new_dit < 30u)
                rx_dit = new_dit;
        }
    } else {
        if      (rx_dur >= rx_dit * 7u) { rx_commit(); rx_push(' '); }
        else if (rx_dur >= rx_dit * 2u)   rx_commit();
    }

    rx_mark = is_mark;
    rx_dur  = 0;
}

// Called from MAIN_ProcessKeys when gCW_Active
void CW_ProcessKey(KEY_Code_t Key, bool pressed, bool held) {
    if (Key >= KEY_0 && Key <= KEY_9) {
        if (pressed && !held && tx_str == NULL) {
            uint8_t num = (uint8_t)(Key - KEY_0);
            uint8_t start = KEY_MAP_OFF[num];
            uint8_t map_len = (uint8_t)(KEY_MAP_OFF[num + 1] - start);

            if (key_timeout_ticks > 0 && Key == last_key && rx_pos > 0) {
                key_idx++;
                if (key_idx >= map_len) key_idx = 0;
                rx_buf[rx_pos - 1] = KEY_MAP_STR[start + key_idx];
            } else {
                if (rx_pos < CW_DISPLAY_LEN) {
                    key_idx = 0;
                    rx_buf[rx_pos++] = KEY_MAP_STR[start];
                    rx_buf[rx_pos]   = 0;
                }
            }
            last_key = Key;
            key_timeout_ticks = 100; // 1 second
        }
        return;
    }

    switch (Key) {
        case KEY_SIDE1:
            if (pressed && !held && !tx_active && tx_str == NULL) q_element(false);
            break;
        case KEY_SIDE2:
            if (pressed && !held && !tx_active && tx_str == NULL) q_element(true);
            break;
        case KEY_UP:
            if (!pressed && !held && cw_wpm_idx < 7) {
                cw_wpm_idx++;
                cw_dit_ticks = WPM_DIT_MAP[cw_wpm_idx];
            }
            break;
        case KEY_DOWN:
            if (!pressed && !held && cw_wpm_idx > 0) {
                cw_wpm_idx--;
                cw_dit_ticks = WPM_DIT_MAP[cw_wpm_idx];
            }
            break;
        case KEY_STAR:
            if (pressed && !held && tx_str == NULL) {
                if (rx_pos > 0) {
                    rx_pos--;
                    rx_buf[rx_pos] = 0;
                    key_timeout_ticks = 0;
                }
            }
            break;
        case KEY_MENU:
            if (pressed && !held && tx_str == NULL && rx_pos > 0) {
                tx_str = rx_buf;
                tx_str_pos = 0;
                tx_char_len = 0;
                key_timeout_ticks = 0;
            }
            break;
        case KEY_F:
            if (pressed && !held) {
                cw_speaker = !cw_speaker;
                if (!cw_speaker) {
                    gEnableSpeaker = false;
                    AUDIO_AudioPathOff();
                } else if (tx_active) {
                    AUDIO_AudioPathOn();
                    gEnableSpeaker = true;
                }
            }
            break;
        case KEY_EXIT:
            if (!pressed && !held) {
                if (tx_str != NULL) {
                    tx_str = NULL;
                    tx_off();
                } else {
                    CW_Deinit();
                }
            }
            break;
        default:
            break;
    }
}

// Render CW screen
void CW_Display(void) {
    char wpm_str[12];

    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

    // Row 0: header
    UI_PrintStringSmall("-CW- SK1=. SK2=-", 0, 127, 0);

    // Row 2: WPM + status + sound output state
    wpm_str[0] = WPM_NAMES[cw_wpm_idx][0];
    wpm_str[1] = WPM_NAMES[cw_wpm_idx][1];
    wpm_str[2] = 'W';
    wpm_str[3] = ' ';
    wpm_str[4] = (tx_str != NULL) ? 'T' : (rx_calib ? 'C' : (rx_mark ? '*' : 'R'));
    wpm_str[5] = (tx_str != NULL) ? 'X' : (rx_calib ? 'A' : 'X');
    wpm_str[6] = rx_calib ? 'L' : ' ';
    wpm_str[7] = ' ';
    wpm_str[8] = cw_speaker ? 'S' : 'M';
    wpm_str[9] = cw_speaker ? 'O' : 'U';
    wpm_str[10] = cw_speaker ? 'N' : 'T';
    wpm_str[11] = 0;
    UI_PrintStringSmall(wpm_str, 0, 127, 2);

    // Row 4: decoded text or message being typed
    UI_PrintStringSmall(rx_buf, 0, 127, 4);

    // Row 6: control menu (one single call to prevent centering overflow crash)
    UI_PrintStringSmall("M=TX *=DEL F=SND EX=QT", 0, 127, 6);

    ST7565_BlitFullScreen();
}
