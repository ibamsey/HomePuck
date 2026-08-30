#include "placeholder_screen.h"
#include "config.h"

static const uint16_t C_BG = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_CYAN = 0x07FF;
static const uint16_t C_DIM = 0x4208;

static const char* screenTitle(UserScreenMode mode) {
    switch (mode) {
        case USER_SCREEN_PLACEHOLDER_A:
            return "Screen 2";
        case USER_SCREEN_PLACEHOLDER_B:
            return "Screen 3";
        default:
            return "Placeholder";
    }
}

void placeholderScreenDraw(LGFX& display, UserScreenMode mode, uint8_t variation) {
    display.fillScreen(C_BG);
    display.drawCircle(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2, 112, C_GREY);

    display.setTextSize(2);
    display.setTextColor(C_CYAN, C_BG);
    display.setTextDatum(TC_DATUM);
    display.drawString(screenTitle(mode), DISPLAY_WIDTH / 2, 70);

    char var_buf[24];
    snprintf(var_buf, sizeof(var_buf), "Var %u", (unsigned)(variation + 1));
    display.setTextSize(1);
    display.setTextColor(C_WHITE, C_BG);
    display.drawString(var_buf, DISPLAY_WIDTH / 2, 120);

    display.setTextColor(C_DIM, C_BG);
    display.drawString("Swipe <- -> screens", DISPLAY_WIDTH / 2, 160);
    display.drawString("Swipe ^ v variations", DISPLAY_WIDTH / 2, 178);
    display.setTextSize(2);
}
