#include "n3ds_input.h"
#include "n3ds_platform_config.h"

#include "../log.h"
#include "../runner.h"
#include "../runner_gamepad.h"
#include "../runner_keyboard.h"
#include "../utils.h"

#include <3ds.h>
#include <stb/ds/stb_ds.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// 3DS buttons -> one virtual gamepad (slot 0, same layout as the Switch port) and the PC keyboard keys.
// Optional test harness: N3DS_SD_DIR "harness.txt", one command per line, frames counted from the first room:
//   press <frame> <BUTTON> [frames]   hold a button (A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT)
//   screenshot <frame>                save the top screen to N3DS_SD_DIR "shots/frame_<n>.png"
//   exit <frame>                      write N3DS_SD_DIR "done.txt" and quit

typedef struct {
    u32 key;
    int32_t gml; // GameMaker virtual key
} N3DSKeyMap;

static const N3DSKeyMap gKeyMap[] = {
    { KEY_DUP, VK_UP },
    { KEY_DDOWN, VK_DOWN },
    { KEY_DLEFT, VK_LEFT },
    { KEY_DRIGHT, VK_RIGHT },
    { KEY_START, VK_ENTER },
    { KEY_SELECT, VK_ESCAPE },
};

typedef struct {
    int32_t kind; // 0 press, 1 screenshot, 2 exit
    int32_t frame;
    int32_t length;
    u32 key;
} N3DSHarnessCmd;

static N3DSHarnessCmd* gHarness = NULL;
static int32_t gFrame = 0;
static u32 gKeysHeldPrev = 0;
static bool gExitRequested = false;

static u32 N3DSInput_keyFromName(const char* name) {
    static const struct { const char* name; u32 key; } names[] = {
        { "A", KEY_A }, { "B", KEY_B }, { "X", KEY_X }, { "Y", KEY_Y }, { "L", KEY_L }, { "R", KEY_R },
        { "ZL", KEY_ZL }, { "ZR", KEY_ZR }, { "START", KEY_START }, { "SELECT", KEY_SELECT },
        { "UP", KEY_DUP }, { "DOWN", KEY_DDOWN }, { "LEFT", KEY_DLEFT }, { "RIGHT", KEY_DRIGHT },
    };
    repeat(sizeof(names) / sizeof(names[0]), i) {
        if (strcasecmp(names[i].name, name) == 0) return names[i].key;
    }
    return 0;
}

static void N3DSInput_loadHarness(void) {
    FILE* f = fopen(N3DS_SD_DIR "harness.txt", "r");
    if (f == NULL) return;
    char line[128];
    while (fgets(line, sizeof(line), f) != NULL) {
        char cmd[32] = "", arg[32] = "";
        int frame = 0, length = 1;
        int n = sscanf(line, "%31s %d %31s %d", cmd, &frame, arg, &length);
        if (n < 2 || cmd[0] == '#') continue;
        N3DSHarnessCmd c = { .frame = frame, .length = 1 };
        if (strcmp(cmd, "press") == 0 && n >= 3) {
            c.kind = 0;
            c.key = N3DSInput_keyFromName(arg);
            c.length = n >= 4 ? length : 1;
            if (c.key == 0) continue;
        } else if (strcmp(cmd, "screenshot") == 0) {
            c.kind = 1;
        } else if (strcmp(cmd, "exit") == 0) {
            c.kind = 2;
        } else {
            continue;
        }
        arrput(gHarness, c);
    }
    fclose(f);
    logInfo("Harness: %d commands from harness.txt\n", (int) arrlen(gHarness));
}

void N3DSInput_init(MAYBE_UNUSED Runner* runner) {
    N3DSInput_loadHarness();
}

static void N3DSInput_fillGamepad(GamepadSlot* slot, u32 held, const circlePosition* circle, const circlePosition* cstick) {
    memcpy(slot->buttonDownPrev, slot->buttonDown, sizeof(slot->buttonDownPrev));
    memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
    memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
    memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));
    memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
    memset(slot->axisValue, 0, sizeof(slot->axisValue));
    slot->connected = true;
    slot->jid = 0;
    snprintf(slot->description, sizeof(slot->description), "Nintendo 3DS");
    snprintf(slot->guid, sizeof(slot->guid), "n3ds-0");

    // Nintendo layout like the Switch port: A/B/Y/X = face 1..4.
    if (held & KEY_A) slot->buttonDown[0] = true;
    if (held & KEY_B) slot->buttonDown[1] = true;
    if (held & KEY_Y) slot->buttonDown[2] = true;
    if (held & KEY_X) slot->buttonDown[3] = true;
    if (held & KEY_L) slot->buttonDown[4] = true;
    if (held & KEY_R) slot->buttonDown[5] = true;
    slot->buttonValue[6] = (held & KEY_ZL) ? 1.0f : 0.0f;
    slot->buttonValue[7] = (held & KEY_ZR) ? 1.0f : 0.0f;
    if (held & KEY_ZL) slot->buttonDown[6] = true;
    if (held & KEY_ZR) slot->buttonDown[7] = true;
    if (held & KEY_SELECT) slot->buttonDown[8] = true;
    if (held & KEY_START) slot->buttonDown[9] = true;
    if (held & KEY_DUP) slot->buttonDown[12] = true;
    if (held & KEY_DDOWN) slot->buttonDown[13] = true;
    if (held & KEY_DLEFT) slot->buttonDown[14] = true;
    if (held & KEY_DRIGHT) slot->buttonDown[15] = true;
    slot->axisValue[0] = (float) circle->dx / 156.0f;
    slot->axisValue[1] = -(float) circle->dy / 156.0f;
    slot->axisValue[2] = (float) cstick->dx / 156.0f;
    slot->axisValue[3] = -(float) cstick->dy / 156.0f;
    repeat(GP_AXIS_COUNT, a) {
        if (slot->axisValue[a] > 1.0f) slot->axisValue[a] = 1.0f;
        if (slot->axisValue[a] < -1.0f) slot->axisValue[a] = -1.0f;
    }
    repeat(GP_BUTTON_COUNT, btn) {
        bool wasDown = slot->buttonDownPrev[btn];
        if (slot->buttonDown[btn] && !wasDown) slot->buttonPressed[btn] = true;
        if (!slot->buttonDown[btn] && wasDown) slot->buttonReleased[btn] = true;
    }
}

u32 N3DSInput_update(Runner* runner) {
    hidScanInput();
    u32 held = hidKeysHeld();
    repeat(arrlen(gHarness), i) {
        N3DSHarnessCmd* c = &gHarness[i];
        if (c->kind == 0 && gFrame >= c->frame && gFrame < c->frame + c->length) held |= c->key;
        if (c->kind == 1 && gFrame == c->frame) N3DSScreenshot_request(gFrame);
        if (c->kind == 2 && gFrame == c->frame) gExitRequested = true;
    }
    circlePosition circle, cstick;
    hidCircleRead(&circle);
    hidCstickRead(&cstick);

    // Circle pad doubles as the D-pad for the keyboard keys.
    u32 dirs = held;
    if (circle.dx < -80) dirs |= KEY_DLEFT;
    if (circle.dx > 80) dirs |= KEY_DRIGHT;
    if (circle.dy > 80) dirs |= KEY_DUP;
    if (circle.dy < -80) dirs |= KEY_DDOWN;
    repeat(sizeof(gKeyMap) / sizeof(gKeyMap[0]), i) {
        bool now = (dirs & gKeyMap[i].key) != 0;
        bool before = (gKeysHeldPrev & gKeyMap[i].key) != 0;
        if (now && !before) RunnerKeyboard_onKeyDown(runner->keyboard, gKeyMap[i].gml);
        if (!now && before) RunnerKeyboard_onKeyUp(runner->keyboard, gKeyMap[i].gml);
    }
    u32 down = dirs & ~gKeysHeldPrev;
    gKeysHeldPrev = dirs;

    N3DSInput_fillGamepad(&runner->gamepads->slots[0], held, &circle, &cstick);
    runner->gamepads->connectedCount = 1;
    gFrame++;
    return down;
}

bool N3DSInput_exitRequested(void) {
    return gExitRequested;
}
