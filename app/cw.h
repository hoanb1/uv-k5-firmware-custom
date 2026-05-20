// app/cw.h
// CW (Morse Code) TX + RX module for Quansheng UV-K5
// RX: polls BK4819 Voice Amplitude (REG_64) every 10ms — no hardware mod needed
// TX Paddle:  Side Key 1 = dit, Side Key 2 = dah
// TX Beacon:  type text with 0-9 keys, MENU/PTT to send
// Activate: F+8

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/keyboard.h"

// --- Configuration ---
#define CW_TONE_HZ       700    // CW sidetone frequency
#define CW_WPM_DEFAULT    15    // default words per minute
#define CW_WPM_MIN         5
#define CW_WPM_MAX        40
#define CW_DISPLAY_LEN    14    // decoded chars shown on screen
#define CW_BEACON_LEN     20    // max text beacon length
#define CW_MIN_THRESH    300    // minimum RX amplitude noise floor

// --- Public API ---
void CW_Init(void);
void CW_Deinit(void);
void CW_Tick(void);                                  // call every 10ms from APP_TimeSlice10ms
void CW_ProcessKey(KEY_Code_t Key, bool pressed, bool held);
void CW_Display(void);

extern bool gCW_Active;   // true when CW mode is on
