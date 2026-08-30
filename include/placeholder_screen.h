#ifndef PLACEHOLDER_SCREEN_H
#define PLACEHOLDER_SCREEN_H

#include <Arduino.h>
#include "LGFX_config.h"
#include "user_screens.h"

#define PLACEHOLDER_VAR_A 0
#define PLACEHOLDER_VAR_B 1

void placeholderScreenDraw(LGFX& display, UserScreenMode mode, uint8_t variation);

#endif // PLACEHOLDER_SCREEN_H
