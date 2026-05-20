// app/cw_decoder.h
// CW (Morse Code) decoder using BK4819 Voice Amplitude register (REG_64)
// No hardware mod required - polls amplitude at ~100Hz
// Enable with: ENABLE_CW_DECODER=1

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define CW_DISPLAY_LEN  16   // characters shown on LCD
#define CW_MIN_THRESH   400  // minimum amplitude threshold (noise floor)
#define CW_POLL_MS      10   // poll interval in milliseconds

void CW_Init(void);
void CW_Tick(void);          // call every 10ms from main loop
void CW_Display(void);
