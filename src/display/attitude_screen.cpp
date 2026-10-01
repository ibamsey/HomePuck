#include "attitude_screen.h"
#include "display_compositor.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Update strategy (see docs/user_screens_architecture.md):
// - Shared DisplayStaticLayer holds the full 240x240 frame buffer.
// - Chrome (title) is baked once per screen entry (DisplayUpdateKind::Full).
// - Every data tick redraws the entire viewport INTO the sprite, then one pushFull().
// - No patchRect + direct panel draws: this screen is ~80% dynamic (sky/sea/hull),
//   so partial patches from a static snapshot cannot work cleanly.

static const uint16_t C_BG = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_DIM = 0x4208;
static const uint16_t C_CYAN = 0x07FF;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_AMBER = 0xFD20;
// Artificial horizon: distinct sky (blue) and ground (tan), not two similar teals.
static const uint16_t C_SKY = 0x3D9F;
static const uint16_t C_GROUND = 0x9B26;
static const uint16_t C_HULL_PORT = 0x0188;
static const uint16_t C_HULL_STBD = 0x39C7;
static const uint16_t C_HULL_DECK = 0x4A69;
static const uint16_t C_KEEL = 0x2965;
static const uint16_t C_LABEL_BG = 0x0841;

static const int16_t CX = DISPLAY_WIDTH / 2;
static const int16_t CY = 108;
static const int16_t VIEW_TOP = 22;
static const int16_t VIEW_BOTTOM = DISPLAY_BADGE_STRIP_Y;
static const int16_t VIEW_H = VIEW_BOTTOM - VIEW_TOP;
static const float PROJ_SCALE = 1.38f;
// Round 240 display clips corners — keep readouts inside ~r=100 from centre.
static const int16_t READOUT_Y = 186;
static const int16_t READOUT_SIDE_Y = 110;

static float s_lastPitch = -999.0f;
static float s_lastRoll = -999.0f;
static float s_lastYaw = -999.0f;
static bool s_lastPitchValid = false;
static bool s_lastRollValid = false;
static bool s_lastYawValid = false;

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Tri {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint16_t color;
};

struct Edge {
    uint8_t a;
    uint8_t b;
};

static const Vec3 kHullVerts[] = {
    {0.0f, 50.0f, 2.0f},
    {-20.0f, 16.0f, 9.0f},
    {20.0f, 16.0f, 9.0f},
    {-24.0f, -14.0f, 7.0f},
    {24.0f, -14.0f, 7.0f},
    {-18.0f, -38.0f, 4.0f},
    {18.0f, -38.0f, 4.0f},
    {0.0f, -38.0f, -7.0f},
    {0.0f, 8.0f, 12.0f},
};

static const Tri kHullFaces[] = {
    {1, 3, 8, C_HULL_PORT},
    {3, 5, 8, C_HULL_PORT},
    {5, 7, 3, C_HULL_PORT},
    {2, 4, 8, C_HULL_STBD},
    {4, 6, 8, C_HULL_STBD},
    {6, 7, 4, C_HULL_STBD},
    {0, 1, 8, C_HULL_DECK},
    {0, 2, 8, C_HULL_DECK},
    {1, 2, 8, C_HULL_DECK},
    {5, 6, 7, C_KEEL},
};

static const Edge kHullOutline[] = {
    {0, 1}, {0, 2}, {1, 2}, {1, 3}, {2, 4}, {3, 4},
    {3, 5}, {4, 6}, {5, 6}, {5, 7}, {6, 7}, {0, 8}, {1, 8}, {2, 8},
    {0, 7},
};

static Vec3 rotateBody(const Vec3& v, float pitchRad, float rollRad, float yawRad) {
    float cr = cosf(rollRad);
    float sr = sinf(rollRad);
    float cp = cosf(pitchRad);
    float sp = sinf(pitchRad);
    float cy = cosf(yawRad);
    float sy = sinf(yawRad);

    float xr = v.x * cr + v.z * sr;
    float yr = v.y;
    float zr = -v.x * sr + v.z * cr;

    float xp = xr;
    float yp = yr * cp - zr * sp;
    float zp = yr * sp + zr * cp;

    return {xp * cy - yp * sy, xp * sy + yp * cy, zp};
}

static void projectPoint(const Vec3& v, int16_t& sx, int16_t& sy) {
    float camY = -0.55f;
    float camZ = 0.48f;
    float vx = v.x;
    float vz = v.y * camZ - v.z * camY;
    sx = CX + (int16_t)(vx * PROJ_SCALE);
    sy = CY - (int16_t)(vz * PROJ_SCALE);
}

static void drawHorizon(lgfx::LovyanGFX& gfx, float pitchDeg, float rollDeg, bool valid) {
    float rollRad = rollDeg * (float)M_PI / 180.0f;
    float offsetY = pitchDeg * 2.2f;
    float halfLen = 140.0f;

    float dx = cosf(rollRad) * halfLen;
    float dy = sinf(rollRad) * halfLen;
    int16_t x0 = CX - (int16_t)dx;
    int16_t y0 = CY + (int16_t)dy + (int16_t)offsetY;
    int16_t x1 = CX + (int16_t)dx;
    int16_t y1 = CY - (int16_t)dy + (int16_t)offsetY;

    uint16_t skyFill = valid ? C_SKY : C_BG;
    uint16_t groundFill = valid ? C_GROUND : C_BG;

    gfx.fillTriangle(0, VIEW_TOP, DISPLAY_WIDTH, VIEW_TOP, x1, y1, skyFill);
    gfx.fillTriangle(0, VIEW_TOP, x0, y0, x1, y1, skyFill);
    gfx.fillTriangle(0, VIEW_BOTTOM, DISPLAY_WIDTH, VIEW_BOTTOM, x0, y0, groundFill);
    gfx.fillTriangle(0, VIEW_BOTTOM, x1, y1, x0, y0, groundFill);
    gfx.drawLine(x0, y0, x1, y1, valid ? C_WHITE : C_DIM);
}

static void drawHull(lgfx::LovyanGFX& gfx, float pitchDeg, float rollDeg, float yawDeg, bool valid) {
    float pitchRad = pitchDeg * (float)M_PI / 180.0f;
    float rollRad = rollDeg * (float)M_PI / 180.0f;
    float yawRad = yawDeg * (float)M_PI / 180.0f;

    int16_t sx[9];
    int16_t sy[9];
    for (uint8_t i = 0; i < 9; i++) {
        Vec3 r = rotateBody(kHullVerts[i], pitchRad, rollRad, yawRad);
        projectPoint(r, sx[i], sy[i]);
    }

    uint16_t outlineColor = valid ? C_WHITE : C_GREY;

    if (valid) {
        for (const Tri& tri : kHullFaces) {
            gfx.fillTriangle(sx[tri.a], sy[tri.a], sx[tri.b], sy[tri.b], sx[tri.c], sy[tri.c], tri.color);
        }
    }

    for (const Edge& e : kHullOutline) {
        gfx.drawLine(sx[e.a], sy[e.a], sx[e.b], sy[e.b], outlineColor);
    }

    if (valid) {
        gfx.fillCircle(sx[0], sy[0], 3, C_AMBER);
    }
}

static void drawValueLabel(lgfx::LovyanGFX& gfx, const char* text,
                           int16_t x, int16_t y, int16_t plateX, int16_t plateY, int16_t plateW,
                           lgfx::textdatum_t datum, uint16_t color) {
    gfx.fillRect(plateX, plateY, plateW, 14, C_LABEL_BG);
    gfx.setTextSize(1);
    gfx.setTextColor(color, C_LABEL_BG);
    gfx.setTextDatum(datum);
    gfx.drawString(text, x, y);
}

static void drawReadouts(lgfx::LovyanGFX& gfx, bool pitchValid, float pitch,
                         bool rollValid, float roll, bool yawValid, float yaw) {
    char buf[16];

    snprintf(buf, sizeof(buf), pitchValid ? "P %+.0f" : "P --");
    drawValueLabel(gfx, buf, 54, READOUT_SIDE_Y, 28, READOUT_SIDE_Y - 6, 52,
                   TL_DATUM, pitchValid ? C_WHITE : C_GREY);

    snprintf(buf, sizeof(buf), rollValid ? "R %+.0f" : "R --");
    drawValueLabel(gfx, buf, 186, READOUT_SIDE_Y, 160, READOUT_SIDE_Y - 6, 52,
                   TR_DATUM, rollValid ? C_WHITE : C_GREY);

    snprintf(buf, sizeof(buf), yawValid ? "Y %.0f" : "Y --");
    drawValueLabel(gfx, buf, CX, READOUT_Y, CX - 28, READOUT_Y - 6, 56,
                   TC_DATUM, yawValid ? C_GREEN : C_GREY);
}

static void buildChrome(lgfx::LGFX_Sprite& sprite) {
    sprite.fillScreen(C_BG);
    sprite.setTextSize(1);
    sprite.setTextColor(C_CYAN, C_BG);
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString("ATT", CX, 14);
}

static void renderViewport(lgfx::LGFX_Sprite& sprite, bool pitchValid, float pitch,
                           bool rollValid, float roll, bool yawValid, float yaw) {
    bool anyValid = pitchValid || rollValid || yawValid;
    float p = pitchValid ? pitch : 0.0f;
    float r = rollValid ? roll : 0.0f;
    float y = yawValid ? yaw : 0.0f;

    sprite.fillRect(0, VIEW_TOP, DISPLAY_WIDTH, VIEW_H, C_BG);
    drawHorizon(sprite, p, r, anyValid);
    sprite.drawCircle(CX, CY, 100, C_DIM);
    drawHull(sprite, p, r, y, anyValid);
    drawReadouts(sprite, pitchValid, pitch, rollValid, roll, yawValid, yaw);
}

static void resetCache(bool pitchValid, float pitch, bool rollValid, float roll,
                       bool yawValid, float yaw) {
    s_lastPitchValid = pitchValid;
    s_lastRollValid = rollValid;
    s_lastYawValid = yawValid;
    s_lastPitch = pitch;
    s_lastRoll = roll;
    s_lastYaw = yaw;
}

bool attitudeScreenDataChanged(const AttitudeData& attitude, bool stale) {
    bool pitchValid = attitude.pitch_valid && !stale;
    bool rollValid = attitude.roll_valid && !stale;
    bool yawValid = attitude.yaw_valid && !stale;

    if (pitchValid != s_lastPitchValid) return true;
    if (rollValid != s_lastRollValid) return true;
    if (yawValid != s_lastYawValid) return true;
    if (pitchValid && fabsf(attitude.pitch_deg - s_lastPitch) > 0.4f) return true;
    if (rollValid && fabsf(attitude.roll_deg - s_lastRoll) > 0.4f) return true;
    if (yawValid && fabsf(attitude.yaw_deg - s_lastYaw) > 1.0f) return true;
    return false;
}

void attitudeScreenDraw(LGFX& display, DisplayUpdateKind kind, const AttitudeData& attitude, bool stale,
                        unsigned long now_ms) {
    (void)now_ms;

    bool pitchValid = attitude.pitch_valid && !stale;
    bool rollValid = attitude.roll_valid && !stale;
    bool yawValid = attitude.yaw_valid && !stale;

    DisplayStaticLayer& layer = displayStaticLayer();
    if (!layer.ensure(display, USER_SCREEN_ATTITUDE)) {
        return;
    }

    lgfx::LGFX_Sprite* sprite = layer.sprite();
    if (sprite == nullptr) {
        return;
    }

    if (kind == DisplayUpdateKind::Full || !layer.isReadyFor(USER_SCREEN_ATTITUDE)) {
        buildChrome(*sprite);
        layer.markReady();
    }

    renderViewport(*sprite, pitchValid, attitude.pitch_deg, rollValid, attitude.roll_deg,
                   yawValid, attitude.yaw_deg);
    layer.pushFull(display);

    resetCache(pitchValid, attitude.pitch_deg, rollValid, attitude.roll_deg,
               yawValid, attitude.yaw_deg);
}
