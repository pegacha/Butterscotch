#include "n3ds_pause.h"

#include "n3ds_gml.h"
#include "n3ds_input.h"
#include "n3ds_renderer.h"

#include "../log.h"
#include "../runner.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// What AM2R's pause screen is made of (from its code):
//  - global.ssmode picks the page: 0 map, 1 inventory, 2 logs, 3 options. Menu2 (oControl.kMenu2) opens a cross
//    (oSubScrChange) where a direction picks the page: up map, right inventory, left logs, down options.
//  - Map: oMapCamera follows targetx/targety (the D-pad pans it 2 px a frame). Menu1 starts placing the marker
//    (oMapCursor.state 0 -> 1), the D-pad moves it one cell per 9 frames (global.mapmarkerx/y, cells 3..76 x 3..59),
//    Menu1 sets it (state 2), Menu2 while placing deletes it.
//  - Inventory (oSubscreenMenu): global.curropt 0..16 = suits, beams, misc, boots, in four fixed panels; items not
//    collected are skipped; Menu1 toggles the selected one.
//  - Logs (oLogScreenControl): global.curropt picks an oLogEntry (its optionid); the list follows the selection;
//    Menu1 expands it, then up/down scroll the text.
//  - Options and their submenus: rows (oPauseOption, oNormalOption[R|C], oOptionLR, oOptionSlider) with optionid;
//    global.curropt is the selected one (changing it also updates the tip text); Menu1 activates; left/right change
//    a value; Menu2 goes back.
// Touch here: tap a row to select it, tap the selected row to activate it (touch and hold the left/right half of
// its value box to turn the value down/up); the hint strip at the bottom (which shows the Menu2 hint) is Menu2; drag the map to pan it and
// tap a cell to put the marker there (tap the marker to delete it); drag a log list or an expanded log to scroll.
// Note: AM2R has a debug leftover that moves Samus to the mouse while mouse button 1 is held, so touch must never
// become a mouse.

#define N3DS_PAUSE_DRAG_THRESHOLD 6
#define N3DS_PAUSE_HINT_STRIP_Y 214
#define N3DS_PAUSE_TOP_STRIP_Y 14
#define N3DS_PAUSE_MAX_STEPS 16
#define N3DS_PAUSE_NAV_LIMIT 64
#define N3DS_PAUSE_SCROLL_STEP 12
// A frame captured on a Start press is the paused game if the pause screen opens within this many frames.
#define N3DS_PAUSE_CAPTURE_FRAMES 30

enum { BTN_UP = 12, BTN_DOWN = 13, BTN_LEFT = 14, BTN_RIGHT = 15 };
enum { BTN_MENU1 = -1, BTN_MENU2 = -2 }; // resolved from the game's own bindings when pressed

typedef enum {
    STEP_PRESS,       // a = button
    STEP_NAVIGATE,    // a = target global.curropt: up/down presses until it gets there
    STEP_WAIT_CROSS,  // wait for the page cross to finish sliding in
    STEP_SET_MARKER,  // a, b = map cell
} N3DSPauseStepKind;

typedef struct {
    N3DSPauseStepKind kind;
    int32_t a, b;
    int32_t tries;
} N3DSPauseStep;

typedef enum { ROW_LEFT, ROW_RIGHT, ROW_CENTRE, ROW_VALUE } N3DSRowAlign;

static struct {
    Runner* runner;
    Renderer* renderer;
    bool paused;
    int32_t roomSubscreen, roomTransition;
    int32_t objSubScrChange, objMapCursor, objMapCamera, objMapMarker, objSubscreenMenu, objLogScreenControl, objLogEntry;
    int32_t rowObjects[6];
    N3DSRowAlign rowAligns[6];
    // touch
    bool touchDown, dragging;
    int32_t holdButton; // held while the touch lasts (a value box), -1 none
    int32_t startX, startY, lastX, lastY, scrollAccum;
    // queued presses
    N3DSPauseStep steps[N3DS_PAUSE_MAX_STEPS];
    int32_t stepCount, stepHead, cooldown;
    uint32_t frame, captureFrame; // captureFrame 0 = nothing held
} gPause;

void N3DSPause_init(Runner* runner, Renderer* renderer) {
    memset(&gPause, 0, sizeof(gPause));
    gPause.runner = runner;
    gPause.renderer = renderer;
    gPause.holdButton = -1;
    N3DSGml_init(runner);
    gPause.roomSubscreen = N3DSGml_roomIndex("rm_subscreen");
    gPause.roomTransition = N3DSGml_roomIndex("rm_transition");
    gPause.objSubScrChange = N3DSGml_objectIndex("oSubScrChange");
    gPause.objMapCursor = N3DSGml_objectIndex("oMapCursor");
    gPause.objMapCamera = N3DSGml_objectIndex("oMapCamera");
    gPause.objMapMarker = N3DSGml_objectIndex("oMapMarker");
    gPause.objSubscreenMenu = N3DSGml_objectIndex("oSubscreenMenu");
    gPause.objLogScreenControl = N3DSGml_objectIndex("oLogScreenControl");
    gPause.objLogEntry = N3DSGml_objectIndex("oLogEntry");
    static const char* rowNames[6] = { "oPauseOption", "oNormalOption", "oNormalOptionR", "oNormalOptionC", "oOptionLR", "oOptionSlider" };
    static const N3DSRowAlign rowAligns[6] = { ROW_LEFT, ROW_LEFT, ROW_RIGHT, ROW_CENTRE, ROW_VALUE, ROW_VALUE };
    for (int i = 0; i < 6; i++) {
        gPause.rowObjects[i] = N3DSGml_objectIndex(rowNames[i]);
        gPause.rowAligns[i] = rowAligns[i];
    }
    if (gPause.roomSubscreen < 0) logInfo("Pause: no rm_subscreen in this game, bottom-screen pause off\n");
}

bool N3DSPause_isPaused(void) {
    return gPause.paused;
}

// ===[ Presses ]===

static int32_t N3DSPause_resolveButton(int32_t button) {
    if (button >= 0) return button;
    double binding = 0.0;
    // GameMaker gamepad constants: gp_face1 = 32769.
    if (!N3DSGml_getGlobal(button == BTN_MENU1 ? "opxjoybtn_menu1" : "opxjoybtn_menu2", -1, &binding)) return -1;
    return (int32_t) binding - 32769;
}

static void N3DSPause_clearSteps(void) {
    gPause.stepCount = 0;
    gPause.stepHead = 0;
}

static void N3DSPause_push(N3DSPauseStepKind kind, int32_t a, int32_t b) {
    if (gPause.stepCount >= N3DS_PAUSE_MAX_STEPS) return;
    gPause.steps[gPause.stepCount++] = (N3DSPauseStep) { .kind = kind, .a = a, .b = b };
}

static void N3DSPause_press(int32_t button) {
    N3DSPause_push(STEP_PRESS, button, 0);
}

static double N3DSPause_curropt(void) {
    double v = -1.0;
    N3DSGml_getGlobal("curropt", -1, &v);
    return v;
}

static void N3DSPause_setMarker(int32_t cx, int32_t cy) {
    N3DSGml_setGlobal("mapmarkerx", cx);
    N3DSGml_setGlobal("mapmarkery", cy);
    Instance* marker = N3DSGml_firstInstance(gPause.objMapMarker);
    if (marker != NULL) {
        N3DSGml_setVar(marker, "x", cx * 8.0);
        N3DSGml_setVar(marker, "y", cy * 8.0 + 32.0);
    }
}

// Runs the queued steps: one press per frame with a free frame after it (the game reacts to the press edge).
static void N3DSPause_runSteps(void) {
    if (gPause.cooldown > 0) {
        gPause.cooldown--;
        return;
    }
    while (gPause.stepHead < gPause.stepCount) {
        N3DSPauseStep* step = &gPause.steps[gPause.stepHead];
        switch (step->kind) {
            case STEP_PRESS: {
                int32_t button = N3DSPause_resolveButton(step->a);
                if (button >= 0) N3DSInput_injectButton(gPause.runner, button);
                gPause.stepHead++;
                gPause.cooldown = 1;
                return;
            }
            case STEP_NAVIGATE: {
                double current = N3DSPause_curropt();
                if ((int32_t) current == step->a || step->tries++ >= N3DS_PAUSE_NAV_LIMIT || current < 0.0) {
                    gPause.stepHead++;
                    continue;
                }
                N3DSInput_injectButton(gPause.runner, step->a > (int32_t) current ? BTN_DOWN : BTN_UP);
                gPause.cooldown = 1;
                return;
            }
            case STEP_WAIT_CROSS: {
                Instance* cross = N3DSGml_firstInstance(gPause.objSubScrChange);
                double state = 0.0;
                if (cross != NULL && N3DSGml_getVar(cross, "state", &state) && state < 1.0 && step->tries++ < 30) return;
                gPause.stepHead++;
                continue;
            }
            case STEP_SET_MARKER:
                N3DSPause_setMarker(step->a, step->b);
                gPause.stepHead++;
                continue;
        }
    }
    N3DSPause_clearSteps();
}

// ===[ Pages ]===

static int32_t N3DSPause_page(void);

// Screen point -> room point (the pause screen is drawn 1:1 through view 0).
static void N3DSPause_toRoom(int32_t sx, int32_t sy, float* rx, float* ry) {
    float vx, vy;
    N3DSGml_viewOrigin(&vx, &vy);
    *rx = vx + (float) sx;
    *ry = vy + (float) sy;
}

static void N3DSPause_tapCross(int32_t x, int32_t y) {
    int32_t dx = x - 160, dy = y - 120;
    if (abs(dx) < 16 && abs(dy) < 16) return;
    N3DSPause_push(STEP_WAIT_CROSS, 0, 0);
    if (abs(dx) > abs(dy)) N3DSPause_press(dx > 0 ? BTN_RIGHT : BTN_LEFT);
    else N3DSPause_press(dy > 0 ? BTN_DOWN : BTN_UP);
}

static void N3DSPause_tapMap(int32_t x, int32_t y) {
    Instance* cursor = N3DSGml_firstInstance(gPause.objMapCursor);
    double state = 0.0, active = 0.0;
    if (cursor == NULL || !N3DSGml_getVar(cursor, "state", &state)) return;
    if (N3DSGml_getVar(cursor, "active", &active) && active == 0.0) return;
    float rx, ry;
    N3DSPause_toRoom(x, y, &rx, &ry);
    int32_t cx = (int32_t) floorf(rx / 8.0f), cy = (int32_t) floorf((ry - 32.0f) / 8.0f);
    if (cx < 3) cx = 3;
    if (cx > 76) cx = 76;
    if (cy < 3) cy = 3;
    if (cy > 59) cy = 59;
    double hasMarker = 0.0, mx = -1.0, my = -1.0;
    N3DSGml_getGlobal("mapmarker", -1, &hasMarker);
    N3DSGml_getGlobal("mapmarkerx", -1, &mx);
    N3DSGml_getGlobal("mapmarkery", -1, &my);
    if ((int32_t) state == 2 && hasMarker != 0.0 && (int32_t) mx == cx && (int32_t) my == cy) {
        // On the marker: delete it (edit, then Menu2).
        N3DSPause_press(BTN_MENU1);
        N3DSPause_press(BTN_MENU2);
        return;
    }
    if ((int32_t) state != 1) N3DSPause_press(BTN_MENU1); // start placing (creates the marker in state 0)
    N3DSPause_push(STEP_SET_MARKER, cx, cy);
    N3DSPause_press(BTN_MENU1); // set it
}

static void N3DSPause_dragMap(int32_t dx, int32_t dy) {
    Instance* camera = N3DSGml_firstInstance(gPause.objMapCamera);
    Instance* cursor = N3DSGml_firstInstance(gPause.objMapCursor);
    double state = 0.0, tx = 0.0, ty = 0.0;
    if (camera == NULL || cursor == NULL || !N3DSGml_getVar(cursor, "state", &state) || (int32_t) state == 1) return;
    if (!N3DSGml_getVar(camera, "targetx", &tx) || !N3DSGml_getVar(camera, "targety", &ty)) return;
    // The camera clamps its own target every step.
    N3DSGml_setVar(camera, "targetx", tx - dx);
    N3DSGml_setVar(camera, "targety", ty - dy);
}

// Inventory rows: four panels of fixed rows (AM2R's oSubScreen{Suit,Beam,Misc,Boots} at (56,56), (56,184),
// (264,56), (264,184)); global.item[] says whether a row's item has been collected (-1: always there).
typedef struct {
    int16_t x, y; // text position on screen
    int8_t item;
} N3DSInventoryRow;

static const N3DSInventoryRow gInventoryRows[17] = {
    { 36, 71, -1 }, { 36, 80, 5 }, { 36, 89, 9 },                                   // suits
    { 36, 137, 10 }, { 36, 146, 11 }, { 36, 155, 12 }, { 36, 164, 13 }, { 36, 173, 14 }, // beams
    { 244, 71, -1 }, { 244, 80, 2 }, { 244, 89, 3 }, { 244, 98, 0 }, { 244, 107, 1 }, { 244, 116, 8 }, // misc
    { 244, 155, 4 }, { 244, 164, 6 }, { 244, 173, 7 },                               // boots
};

static void N3DSPause_tapInventory(int32_t x, int32_t y) {
    Instance* menu = N3DSGml_firstInstance(gPause.objSubscreenMenu);
    double active = 0.0;
    if (menu == NULL || !N3DSGml_getVar(menu, "active", &active) || active == 0.0) return;
    for (int32_t i = 0; i < 17; i++) {
        const N3DSInventoryRow* row = &gInventoryRows[i];
        if (x < row->x - 12 || x > row->x + 72 || y < row->y - 1 || y > row->y + 8) continue;
        double owned = 1.0;
        if (row->item >= 0 && (!N3DSGml_getGlobal("item", row->item, &owned) || owned == 0.0)) return;
        if ((int32_t) N3DSPause_curropt() == i) N3DSPause_press(BTN_MENU1); // toggle
        else N3DSGml_setGlobal("curropt", i);                               // the panels draw the tip from it
        return;
    }
}

static void N3DSPause_tapLogs(int32_t x, int32_t y) {
    Instance* control = N3DSGml_firstInstance(gPause.objLogScreenControl);
    double active = 0.0, expanded = 0.0;
    if (control == NULL || !N3DSGml_getVar(control, "active", &active) || active == 0.0) return;
    N3DSGml_getVar(control, "expanded", &expanded);
    if (expanded != 0.0) {
        N3DSPause_press(BTN_MENU1); // collapse
        return;
    }
    float rx, ry;
    N3DSPause_toRoom(x, y, &rx, &ry);
    Instance* entries[64];
    int32_t n = N3DSGml_instances(gPause.objLogEntry, entries, 64);
    for (int32_t i = 0; i < n; i++) {
        double optionId = -1.0;
        if (!N3DSGml_getVar(entries[i], "optionid", &optionId)) continue;
        float ex = entries[i]->x, ey = entries[i]->y;
        if (rx < ex - 6.0f || rx > ex + 140.0f || ry < ey - 6.0f || ry > ey + 9.0f) continue;
        if ((int32_t) N3DSPause_curropt() == (int32_t) optionId) N3DSPause_press(BTN_MENU1); // expand
        else N3DSPause_push(STEP_NAVIGATE, (int32_t) optionId, 0);
        return;
    }
}

// Options pages: the row object (any type with an optionid) under a screen point, NULL if none.
static Instance* N3DSPause_optionRowAt(int32_t x, int32_t y, N3DSRowAlign* align) {
    float rx, ry;
    N3DSPause_toRoom(x, y, &rx, &ry);
    Instance* best = NULL;
    float bestDistance = 1e9f;
    for (int t = 0; t < 6; t++) {
        Instance* rows[32];
        int32_t n = N3DSGml_instances(gPause.rowObjects[t], rows, 32);
        for (int32_t i = 0; i < n; i++) {
            if (!rows[i]->visible) continue;
            float ex = rows[i]->x, ey = rows[i]->y;
            float left = ex - 6.0f, right = ex + 160.0f;
            if (gPause.rowAligns[t] == ROW_RIGHT) { left = ex - 160.0f; right = ex + 6.0f; }
            if (gPause.rowAligns[t] == ROW_CENTRE) { left = ex - 80.0f; right = ex + 80.0f; }
            if (gPause.rowAligns[t] == ROW_VALUE) {
                double textOffset = 0.0, w = 80.0;
                N3DSGml_getVar(rows[i], "textoffset", &textOffset);
                N3DSGml_getVar(rows[i], "w", &w);
                right = ex + (float) textOffset + (float) (w > 80.0 ? w : 80.0) + 6.0f;
            }
            if (rx < left || rx > right || ry < ey - 6.0f || ry > ey + 10.0f) continue;
            float distance = fabsf(ry - (ey + 2.0f));
            if (distance < bestDistance) {
                bestDistance = distance;
                best = rows[i];
                *align = gPause.rowAligns[t];
            }
        }
    }
    return best;
}

static bool N3DSPause_isSelectedRow(Instance* row) {
    double optionId = -1.0;
    return row != NULL && N3DSGml_getVar(row, "optionid", &optionId) && (int32_t) N3DSPause_curropt() == (int32_t) optionId;
}

// A touch that starts on the selected row's value box (sliders, left/right choices) holds left or right for as
// long as the finger stays down: AM2R's volume sliders only move while the direction is held.
static int32_t N3DSPause_valueHoldAt(int32_t x, int32_t y) {
    if (N3DSPause_page() != 3) return -1;
    N3DSRowAlign align = ROW_LEFT;
    Instance* row = N3DSPause_optionRowAt(x, y, &align);
    if (row == NULL || align != ROW_VALUE || !N3DSPause_isSelectedRow(row)) return -1;
    float rx, ry;
    N3DSPause_toRoom(x, y, &rx, &ry);
    double textOffset = 0.0, w = 80.0;
    N3DSGml_getVar(row, "textoffset", &textOffset);
    N3DSGml_getVar(row, "w", &w);
    float valueLeft = row->x + (float) textOffset - 4.0f;
    if (rx < valueLeft) return -1;
    return rx < valueLeft + (float) (w > 80.0 ? w : 80.0) * 0.5f ? BTN_LEFT : BTN_RIGHT;
}

static void N3DSPause_tapOptions(int32_t x, int32_t y) {
    N3DSRowAlign align = ROW_LEFT;
    Instance* row = N3DSPause_optionRowAt(x, y, &align);
    double optionId = -1.0, enabled = 1.0;
    if (row == NULL || !N3DSGml_getVar(row, "optionid", &optionId)) return;
    if (N3DSGml_getVar(row, "enabled", &enabled) && enabled == 0.0) return;
    if (!N3DSPause_isSelectedRow(row)) N3DSPause_push(STEP_NAVIGATE, (int32_t) optionId, 0);
    else N3DSPause_press(BTN_MENU1);
}

static int32_t N3DSPause_page(void) {
    Instance* cross = N3DSGml_firstInstance(gPause.objSubScrChange);
    if (cross != NULL) return -1;
    double mode = 0.0;
    N3DSGml_getGlobal("ssmode", -1, &mode);
    return (int32_t) mode;
}

static void N3DSPause_tap(int32_t x, int32_t y) {
    int32_t page = N3DSPause_page();
    if (page == -1) {
        N3DSPause_tapCross(x, y);
        return;
    }
    if (y >= N3DS_PAUSE_HINT_STRIP_Y || y < N3DS_PAUSE_TOP_STRIP_Y) {
        N3DSPause_press(BTN_MENU2);
        return;
    }
    switch (page) {
        case 0: N3DSPause_tapMap(x, y); break;
        case 1: N3DSPause_tapInventory(x, y); break;
        case 2: N3DSPause_tapLogs(x, y); break;
        default: N3DSPause_tapOptions(x, y); break;
    }
}

static void N3DSPause_drag(int32_t dx, int32_t dy) {
    int32_t page = N3DSPause_page();
    if (page == 0) {
        N3DSPause_dragMap(dx, dy);
        return;
    }
    if (page != 2 && page != 3) return;
    // Lists: a press per N pixels (finger up = further down the list / text).
    gPause.scrollAccum += dy;
    while (gPause.scrollAccum <= -N3DS_PAUSE_SCROLL_STEP && gPause.stepCount < N3DS_PAUSE_MAX_STEPS) {
        N3DSPause_press(BTN_DOWN);
        gPause.scrollAccum += N3DS_PAUSE_SCROLL_STEP;
    }
    while (gPause.scrollAccum >= N3DS_PAUSE_SCROLL_STEP && gPause.stepCount < N3DS_PAUSE_MAX_STEPS) {
        N3DSPause_press(BTN_UP);
        gPause.scrollAccum -= N3DS_PAUSE_SCROLL_STEP;
    }
}

static void N3DSPause_handleTouch(void) {
    int32_t x, y;
    bool held = N3DSInput_touch(&x, &y);
    if (held && !gPause.touchDown) {
        gPause.touchDown = true;
        gPause.dragging = false;
        gPause.startX = gPause.lastX = x;
        gPause.startY = gPause.lastY = y;
        gPause.scrollAccum = 0;
        gPause.holdButton = N3DSPause_valueHoldAt(x, y);
        if (gPause.holdButton >= 0) {
            N3DSPause_clearSteps();
            N3DSInput_injectButton(gPause.runner, gPause.holdButton);
        }
        return;
    }
    if (held && gPause.holdButton >= 0) {
        N3DSInput_injectButton(gPause.runner, gPause.holdButton);
        return;
    }
    if (held) {
        if (!gPause.dragging && (abs(x - gPause.startX) > N3DS_PAUSE_DRAG_THRESHOLD || abs(y - gPause.startY) > N3DS_PAUSE_DRAG_THRESHOLD)) {
            gPause.dragging = true;
            N3DSPause_clearSteps();
        }
        if (gPause.dragging) N3DSPause_drag(x - gPause.lastX, y - gPause.lastY);
        gPause.lastX = x;
        gPause.lastY = y;
        return;
    }
    if (gPause.touchDown) {
        gPause.touchDown = false;
        if (gPause.holdButton >= 0) {
            gPause.holdButton = -1;
            return;
        }
        if (!gPause.dragging) {
            // A new tap replaces whatever the last one was still doing.
            N3DSPause_clearSteps();
            N3DSPause_tap(gPause.startX, gPause.startY);
        }
    }
}

// ===[ Frame ]===

static bool N3DSPause_inPauseRoom(void) {
    Runner* runner = gPause.runner;
    if (gPause.roomSubscreen < 0 || runner->currentRoom == NULL) return false;
    int32_t room = runner->currentRoomIndex;
    if (room == gPause.roomSubscreen) return true;
    if (room != gPause.roomTransition) return false;
    // The transition into the pause screen (room_change(rm_subscreen, ...)).
    double target = -1.0;
    return N3DSGml_getGlobal("targetroom", -1, &target) && (int32_t) target == gPause.roomSubscreen;
}

bool N3DSPause_update(void) {
    bool paused = N3DSPause_inPauseRoom();
    gPause.frame++;
    // AM2R blanks the screen on the frame it leaves for the pause screen, so the frame to keep is taken when Start
    // goes into the input delay line (the game sees it a few frames later): the top screen still shows play.
    if (!paused && N3DSInput_startComing() && N3DSRenderer_captureFrozenTop(gPause.renderer)) gPause.captureFrame = gPause.frame;
    if (!paused && gPause.captureFrame != 0 && gPause.frame - gPause.captureFrame > N3DS_PAUSE_CAPTURE_FRAMES) {
        N3DSRenderer_dropFrozenTop(gPause.renderer);
        gPause.captureFrame = 0;
    }
    if (paused && !gPause.paused) {
        if (gPause.captureFrame == 0) N3DSRenderer_captureFrozenTop(gPause.renderer);
        gPause.captureFrame = 0;
        N3DSPause_clearSteps();
        gPause.touchDown = false;
        logInfo("Pause: on the bottom screen\n");
    } else if (!paused && gPause.paused) {
        N3DSRenderer_dropFrozenTop(gPause.renderer);
        N3DSPause_clearSteps();
        logInfo("Pause: back to the game\n");
    }
    gPause.paused = paused;
    N3DSRenderer_setHostScreenBottom(gPause.renderer, paused);
    if (!paused) return false;
    if (gPause.runner->currentRoomIndex == gPause.roomSubscreen) {
        N3DSPause_handleTouch();
        N3DSPause_runSteps();
    }
    return true;
}
