#include "anchor_watch_screen.h"
#include "display_compositor.h"
#include "config.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const uint16_t C_BG = 0x0000;
static const uint16_t C_WHITE = 0xFFFF;
static const uint16_t C_GREY = 0x8410;
static const uint16_t C_DIM = 0x4208;
static const uint16_t C_CYAN = 0x07FF;
static const uint16_t C_GREEN = 0x07E0;
static const uint16_t C_AMBER = 0xFD20;
static const uint16_t C_RED = 0xF800;
static const uint16_t C_TRACE = 0x294A;

static const int16_t CX = DISPLAY_WIDTH / 2;
static const int16_t CY = 112;
static const int16_t MAX_RING_PX = 92;
static const float PX_PER_M = (float)MAX_RING_PX / ANCHOR_MAX_RADIUS_M;

struct TracePoint {
    float east_m;
    float north_m;
};

static bool s_zeroValid = false;
static double s_anchorLat = 0.0;
static double s_anchorLon = 0.0;
static TracePoint s_trace[ANCHOR_TRACE_MAX_POINTS];
static uint16_t s_traceCount = 0;
static float s_lastEastM = 0.0f;
static float s_lastNorthM = 0.0f;
static float s_lastDistM = -1.0f;
static bool s_lastPositionValid = false;
static bool s_lastZeroValid = false;

static void latLonToOffsetM(double lat, double lon, double refLat, double refLon,
                            float& eastM, float& northM) {
    const double kDegToM = 111320.0;
    northM = (float)((lat - refLat) * kDegToM);
    eastM = (float)((lon - refLon) * kDegToM * cos(refLat * M_PI / 180.0));
}

static void offsetToPixel(float eastM, float northM, int16_t& x, int16_t& y) {
    x = CX + (int16_t)(eastM * PX_PER_M);
    y = CY - (int16_t)(northM * PX_PER_M);
}

static float distanceM(float eastM, float northM) {
    return sqrtf(eastM * eastM + northM * northM);
}

static void appendTracePoint(float eastM, float northM) {
    if (s_traceCount > 0) {
        float de = eastM - s_trace[s_traceCount - 1].east_m;
        float dn = northM - s_trace[s_traceCount - 1].north_m;
        if ((de * de + dn * dn) < (ANCHOR_TRACE_MIN_STEP_M * ANCHOR_TRACE_MIN_STEP_M)) {
            return;
        }
    }

    if (s_traceCount >= ANCHOR_TRACE_MAX_POINTS) {
        memmove(&s_trace[0], &s_trace[1], (ANCHOR_TRACE_MAX_POINTS - 1) * sizeof(TracePoint));
        s_traceCount = ANCHOR_TRACE_MAX_POINTS - 1;
    }

    s_trace[s_traceCount].east_m = eastM;
    s_trace[s_traceCount].north_m = northM;
    s_traceCount++;
}

static void buildStaticLayer(lgfx::LGFX_Sprite& sprite) {
    sprite.fillScreen(C_BG);

    for (int ring = 1; ring <= ANCHOR_RING_COUNT; ring++) {
        int16_t r = (int16_t)(ring * ANCHOR_RING_SPACING_M * PX_PER_M);
        uint16_t color = (ring % 2 == 0) ? C_GREY : C_DIM;
        sprite.drawCircle(CX, CY, r, color);
    }

    sprite.drawLine(CX - 6, CY, CX + 6, CY, C_AMBER);
    sprite.drawLine(CX, CY - 6, CX, CY + 6, C_AMBER);
    sprite.fillCircle(CX, CY, 2, C_AMBER);

    for (int ring = 2; ring <= ANCHOR_RING_COUNT; ring += 2) {
        int16_t lx = CX + (int16_t)(ring * ANCHOR_RING_SPACING_M * PX_PER_M) + 4;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", ring * (int)ANCHOR_RING_SPACING_M);
        sprite.setTextSize(1);
        sprite.setTextColor(C_DIM, C_BG);
        sprite.setTextDatum(ML_DATUM);
        sprite.drawString(buf, lx, CY);
    }

    sprite.setTextSize(1);
    sprite.setTextColor(C_CYAN, C_BG);
    sprite.setTextDatum(TC_DATUM);
    sprite.drawString("ANCHOR", CX, 14);
    sprite.drawString("m", CX + 18, CY - 4);
}

static void drawTrace(LGFX& gfx) {
    if (s_traceCount < 2) return;

    int16_t px0, py0, px1, py1;
    offsetToPixel(s_trace[0].east_m, s_trace[0].north_m, px0, py0);

    for (uint16_t i = 1; i < s_traceCount; i++) {
        offsetToPixel(s_trace[i].east_m, s_trace[i].north_m, px1, py1);
        gfx.drawLine(px0, py0, px1, py1, C_TRACE);
        gfx.drawLine(px0 + 1, py0, px1 + 1, py1, C_TRACE);
        px0 = px1;
        py0 = py1;
    }

    for (uint16_t i = 0; i < s_traceCount; i++) {
        int16_t tx, ty;
        offsetToPixel(s_trace[i].east_m, s_trace[i].north_m, tx, ty);
        gfx.fillCircle(tx, ty, 1, C_TRACE);
    }
}

static void drawBoat(LGFX& gfx, float eastM, float northM, bool valid) {
    int16_t bx, by;
    offsetToPixel(eastM, northM, bx, by);

    uint16_t hull = valid ? C_WHITE : C_GREY;
    gfx.fillCircle(bx, by, 5, hull);
    gfx.drawCircle(bx, by, 6, valid ? C_CYAN : C_GREY);
    gfx.fillCircle(bx, by - 4, 2, valid ? C_GREEN : C_GREY);
}

static void drawDynamicOverlay(LGFX& gfx, DisplayStaticLayer& layer, bool positionValid,
                               float eastM, float northM, float distM) {
    layer.patchRect(gfx, CX - MAX_RING_PX - 4, CY - MAX_RING_PX - 4,
                    (MAX_RING_PX + 4) * 2, (MAX_RING_PX + 4) * 2);

    if (s_zeroValid) {
        drawTrace(gfx);
        if (positionValid) {
            drawBoat(gfx, eastM, northM, true);
        }
    } else if (positionValid) {
        drawBoat(gfx, 0.0f, 0.0f, true);
    }

    gfx.fillRect(CX - 64, 182, 128, 28, C_BG);

    char statusBuf[24];
    if (!positionValid) {
        snprintf(statusBuf, sizeof(statusBuf), "NO GPS");
    } else if (!s_zeroValid) {
        snprintf(statusBuf, sizeof(statusBuf), "DBL TAP ZERO");
    } else {
        snprintf(statusBuf, sizeof(statusBuf), "%.0fm", distM);
    }

    uint16_t statusColor = C_WHITE;
    if (!positionValid) {
        statusColor = C_GREY;
    } else if (s_zeroValid && distM > 200.0f) {
        statusColor = C_RED;
    } else if (s_zeroValid && distM > 100.0f) {
        statusColor = C_AMBER;
    } else if (s_zeroValid) {
        statusColor = C_GREEN;
    } else {
        statusColor = C_CYAN;
    }

    gfx.setTextSize(2);
    gfx.setTextColor(statusColor, C_BG);
    gfx.setTextDatum(TC_DATUM);
    gfx.drawString(statusBuf, CX, 196);
}

static void updateMotion(const PositionData& position, bool positionValid) {
    if (!positionValid || !s_zeroValid) return;

    float eastM = 0.0f;
    float northM = 0.0f;
    latLonToOffsetM(position.latitude, position.longitude, s_anchorLat, s_anchorLon, eastM, northM);
    appendTracePoint(eastM, northM);
    s_lastEastM = eastM;
    s_lastNorthM = northM;
}

bool anchorWatchScreenZero(const PositionData& position) {
    if (!position.valid) return false;

    s_anchorLat = position.latitude;
    s_anchorLon = position.longitude;
    s_zeroValid = true;
    s_traceCount = 0;
    s_lastEastM = 0.0f;
    s_lastNorthM = 0.0f;
    s_lastDistM = -1.0f;
    s_lastZeroValid = false;
    s_lastPositionValid = false;
    appendTracePoint(0.0f, 0.0f);
    return true;
}

bool anchorWatchScreenDataChanged(const PositionData& position, bool stale) {
    bool positionValid = position.valid && !stale;

    if (positionValid != s_lastPositionValid) return true;
    if (s_zeroValid != s_lastZeroValid) return true;
    if (!positionValid) return false;

    if (!s_zeroValid) return false;

    float eastM = 0.0f;
    float northM = 0.0f;
    latLonToOffsetM(position.latitude, position.longitude, s_anchorLat, s_anchorLon, eastM, northM);
    float distM = distanceM(eastM, northM);

    if (fabsf(eastM - s_lastEastM) > 0.4f || fabsf(northM - s_lastNorthM) > 0.4f) return true;
    if (fabsf(distM - s_lastDistM) > 0.5f) return true;
    return false;
}

void anchorWatchScreenDraw(LGFX& display, DisplayUpdateKind kind, const PositionData& position, bool stale,
                           unsigned long now_ms) {
    (void)now_ms;

    bool positionValid = position.valid && !stale;

    if (positionValid && s_zeroValid) {
        updateMotion(position, positionValid);
    }

    float eastM = 0.0f;
    float northM = 0.0f;
    float distM = 0.0f;
    if (positionValid && s_zeroValid) {
        latLonToOffsetM(position.latitude, position.longitude, s_anchorLat, s_anchorLon, eastM, northM);
        distM = distanceM(eastM, northM);
    }

    DisplayStaticLayer& layer = displayStaticLayer();
    layer.ensure(display, USER_SCREEN_ANCHOR);

    if (kind == DisplayUpdateKind::Full || !layer.isReadyFor(USER_SCREEN_ANCHOR)) {
        buildStaticLayer(*layer.sprite());
        layer.markReady();
        layer.pushFull(display);
        drawDynamicOverlay(display, layer, positionValid, eastM, northM, distM);
    } else {
        drawDynamicOverlay(display, layer, positionValid, eastM, northM, distM);
    }

    s_lastPositionValid = positionValid;
    s_lastZeroValid = s_zeroValid;
    s_lastEastM = eastM;
    s_lastNorthM = northM;
    s_lastDistM = distM;
}
