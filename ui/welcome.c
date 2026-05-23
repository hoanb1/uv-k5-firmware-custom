/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */
#include "driver/uart.h"
#include <string.h>
#include "driver/keyboard.h"
#include "driver/eeprom.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "helper/battery.h"
#include "settings.h"
#include "misc.h"
#include "ui/helper.h"
#include "ui/welcome.h"
#include "ui/status.h"
#include "version.h"
#include "driver/system.h"
//void UI_DisplayReleaseKeys(BOOT_Mode_t BootMode) {
//    memset(gStatusLine, 0, sizeof(gStatusLine));
//    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));
//
//    if (BootMode == BOOT_MODE_F_LOCK) {



//        ST7565_BlitStatusLine();  // blank status line
//        ST7565_BlitFullScreen();
//    }
//
//
//}

void UI_DisplayWelcome(void) {
    memset(gStatusLine, 0, sizeof(gStatusLine));
    UI_DisplayClear();
    ST7565_BlitStatusLine();
    ST7565_BlitFullScreen();
    BACKLIGHT_TurnOn();
}
