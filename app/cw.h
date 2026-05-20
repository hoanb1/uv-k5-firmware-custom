// app/cw.h
// CW (Morse Code) TX + RX module for Quansheng UV-K5
//
// RX uses BK4819 RSSI (REG_67) — works in FM mode, no hardware mod needed.
//    A CW carrier raises RSSI when ON, drops when OFF.
//    Hysteresis thresholds prevent false transitions from RSSI jitter.
//
// TX paddle: Side Key 1 = dit, Side Key 2 = dah
// Toggle:    Long Press 9
// Exit:      EXIT key

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/keyboard.h"

// --- Configuration ---
#define CW_TONE_HZ          700   // CW sidetone frequency for TX
#define CW_WPM_DEFAULT       15   // starting words per minute
#define CW_WPM_MIN            5
#define CW_WPM_MAX           40
#define CW_DISPLAY_LEN       14   // decoded chars visible on screen

// RSSI thresholds (raw REG_67[8:0] units, 0..511)
// The decoder computes adaptive thresholds from noise/signal levels.
// This is the minimum delta (signal - noise) needed to start decoding.
#define CW_RSSI_MIN_DELTA    12   // ignore signal if S/N difference < this
#define CW_RSSI_NOISE_INIT   30   // initial noise floor guess (raw RSSI units)

// --- Public API ---
void CW_Init(void);
void CW_Deinit(void);
void CW_Tick(void);               // call every 10ms from APP_TimeSlice10ms
void CW_ProcessKey(KEY_Code_t Key, bool pressed, bool held);
void CW_Display(void);

extern bool gCW_Active;           // true when CW mode is on
