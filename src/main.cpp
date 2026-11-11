#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <Adafruit_NeoPixel.h>
#include <ST7567.h>
#include <esp_sleep.h>
#include <esp_bt.h>
#include "driver/gpio.h"

#include "config.h"
#include "pet.h"
#include "ui.h"
#include "animation.h"
#include "sprites.h"
#include "battle.h"

extern ST7567 display;

// ======================================================
// HARDWARE
// ======================================================

Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ======================================================
// BUTTON DEBOUNCE
// ======================================================

static bool prevLeft = false;
static bool prevOk = false;
static bool prevRight = false;
static bool skipNextLeftRelease = false;
static bool skipNextOkRelease = false;
static bool skipNextRightRelease = false;

static bool btnLeftPressed() { return !digitalRead(BTN_LEFT); }
static bool btnOkPressed() { return !digitalRead(BTN_OK); }
static bool btnRightPressed() { return !digitalRead(BTN_RIGHT); }

static bool leftRelease()
{
    bool v = btnLeftPressed();
    bool c = !v && prevLeft;
    if (c && skipNextLeftRelease) { skipNextLeftRelease = false; c = false; }
    prevLeft = v;
    return c;
}

static bool okRelease()
{
    bool v = btnOkPressed();
    bool c = !v && prevOk;
    if (c && skipNextOkRelease) { skipNextOkRelease = false; c = false; }
    prevOk = v;
    return c;
}

static bool rightRelease()
{
    bool v = btnRightPressed();
    bool c = !v && prevRight;
    if (c && skipNextRightRelease) { skipNextRightRelease = false; c = false; }
    prevRight = v;
    return c;
}

static void consumeAllButtons()
{
    skipNextLeftRelease = btnLeftPressed();
    skipNextOkRelease = btnOkPressed();
    skipNextRightRelease = btnRightPressed();
    prevLeft = skipNextLeftRelease;
    prevOk = skipNextOkRelease;
    prevRight = skipNextRightRelease;
}

// ======================================================
// BUZZER
// ======================================================

static void playTone(int freq, int dur)
{
    ledcWriteTone(0, freq);
    delay(dur);
    ledcWrite(0, 0);
}

static void beepLeft()      { playTone(880, 60); }
static void beepRight()    { playTone(660, 60); }
static void beepSelect()  { playTone(1100, 50); }
static void beepConfirm() { playTone(1320, 60); delay(50); playTone(1540, 60); }
static void beepError()   { playTone(330, 100); delay(30); playTone(220, 100); }
static void beepAction()  { playTone(700, 50); delay(30); playTone(900, 50); }
static void beepNeed()    { playTone(880, 60); delay(40); playTone(660, 100); }  // "хочу заботы"
static void beepDead()    { playTone(400,150); delay(100); playTone(300,150); delay(100); playTone(200,250); }

// ======================================================
// NEOPIXEL EFFECTS
// ======================================================

// Per-channel maximum-brightness limiter. Every colour written to the strip passes
// through here, so NEOPIXEL_MAX_R/G/B cap each channel independently (white balance /
// hardware calibration) without touching any effect code. 255 = channel unchanged.
static uint32_t ledScale(uint32_t color)
{
    uint8_t r = (uint16_t)((color >> 16) & 0xFF) * NEOPIXEL_MAX_R / 255;
    uint8_t g = (uint16_t)((color >>  8) & 0xFF) * NEOPIXEL_MAX_G / 255;
    uint8_t b = (uint16_t)( color        & 0xFF) * NEOPIXEL_MAX_B / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Fill the whole strip with a colour (after per-channel scaling) and latch it.
static void ledFill(uint32_t color)
{
    pixels.fill(ledScale(color));
    pixels.show();
}

static void glowPixel(uint32_t color, int dur)
{
    ledFill(color);
    delay(dur);
    pixels.fill(0);
    pixels.show();
}

static void glowAll(uint32_t color, int dur)
{
    uint32_t step = dur / 10;
    for (int i = 0; i < 5; i++)
    {
        uint8_t bright = map(i, 0, 4, 40, 255);
        uint32_t c = ((color >> 16 & 0xFF) * bright / 255) << 16 |
                     ((color >> 8  & 0xFF) * bright / 255) << 8  |
                     ((color       & 0xFF) * bright / 255);
        ledFill(c);
        delay(step);
    }
    delay(step * 2);
    for (int i = 4; i >= 0; i--)
    {
        uint8_t bright = map(i, 0, 4, 40, 255);
        uint32_t c = ((color >> 16 & 0xFF) * bright / 255) << 16 |
                     ((color >> 8  & 0xFF) * bright / 255) << 8  |
                     ((color       & 0xFF) * bright / 255);
        ledFill(c);
        delay(step);
    }
    pixels.fill(0);
    pixels.show();
}

static const uint32_t COLOR_FEED  = 0x2233FF;
static const uint32_t COLOR_PLAY  = 0xFF44FF;
static const uint32_t COLOR_SLEEP = 0x4466FF;
static const uint32_t COLOR_CLEAN = 0x44AAFF;
static const uint32_t COLOR_HEAL  = 0x00FF44;
static const uint32_t COLOR_DEAD  = 0x440000;

// ======================================================
// AMBIENT GLOW
// ======================================================

static uint32_t lastActivityTime = 0;
static uint32_t ambientColor = 0xCCDEAD;
static bool hasBooted = false;
static Screen screenBeforeConfirmReset = SCREEN_MAIN;

static const uint32_t PET_AMBIENT_COLORS[3] = {
    0x00FF00, // Nature
    0x2222FF, // Techno
    0xFF0202  // Rock
};

static void updateAmbientColor()
{
    uint8_t idx = (uint8_t)pet.type;
    if (idx >= 3) idx = 0;
    ambientColor = PET_AMBIENT_COLORS[idx];
}

// Scale a 0-255 channel by a percentage, clamped so it never wraps a uint8_t.
static inline uint8_t scaleChannel(uint8_t c, uint16_t pct)
{
    uint16_t v = (uint16_t)c * pct / 100;
    return v > 255 ? 255 : (uint8_t)v;
}

static void setAmbientGlow()
{
    if ((!pet.evolved && uiGetScreen() != SCREEN_SELECT_TYPE) || uiGetScreen() == SCREEN_WELCOME)
    {
        ledFill(pixels.Color(255, 255, 255));
        return;
    }
    uint8_t r = scaleChannel((ambientColor >> 16) & 0xFF, AMBIENT_BRIGHTNESS);
    uint8_t g = scaleChannel((ambientColor >> 8) & 0xFF, AMBIENT_BRIGHTNESS);
    uint8_t b = scaleChannel(ambientColor & 0xFF, AMBIENT_BRIGHTNESS);
    ledFill(pixels.Color(r, g, b));
}

static void clearLeds()
{
    pixels.fill(0);
    pixels.show();
}

// ======================================================
// NVS SAVE / LOAD
// ======================================================

static const char* NVS_NS = "tamadata";

// Lightweight NVS write: saves only the pet name (single key, no GC pressure).
static void savePetName()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "name", pet.name.c_str());
    nvs_commit(h);
    nvs_close(h);
}

static void savePet()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "name", pet.name.c_str());
    nvs_set_i8(h, "type", (int8_t)pet.type);
    nvs_set_i32(h, "hunger", pet.hunger);
    nvs_set_i32(h, "happy", pet.happiness);
    nvs_set_i32(h, "energy", pet.energy);
    nvs_set_i32(h, "health", pet.health);
    nvs_set_i32(h, "clean", pet.cleanliness);
    nvs_set_i32(h, "xp", (int32_t)pet.xp);
    nvs_set_i32(h, "xpneed", (int32_t)pet.xpToNext);
    nvs_set_i32(h, "level", pet.level);
    nvs_set_i32(h, "age", pet.ageDays);
    nvs_set_i32(h, "steps", (int32_t)pet.steps);
    nvs_set_i32(h, "dead", (int32_t)pet.dead);
    nvs_set_i32(h, "sick", (int32_t)pet.sick);
    nvs_set_i32(h, "sleep", (int32_t)pet.sleeping);
    nvs_set_i32(h, "evolved", (int32_t)pet.evolved);
    nvs_set_i8(h, "setup", pet.setupDone ? 1 : 0);
    nvs_set_i32(h, "stage", pet.stage);
    nvs_set_i32(h, "incub", (int32_t)pet.incubationProgress);
    nvs_set_i32(h, "feedc", pet.feedCount);
    nvs_set_i32(h, "playc", pet.playCount);
    nvs_set_i32(h, "sleepc", pet.sleepCount);
    nvs_set_i32(h, "cleanc", pet.cleanCount);
    nvs_set_i32(h, "healed", pet.healedHp);
    nvs_set_i32(h, "bwon", pet.battlesWon);
    nvs_set_i32(h, "blost", pet.battlesLost);
    nvs_set_i32(h, "totxp", (int32_t)pet.totalXp);
    nvs_commit(h);
    nvs_close(h);
}

// returns true if previously booted (NVS had saved data)
static bool loadPet()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK)
    {
        petReset();
        return false;
    }
    // Start from sane defaults so a key missing from an older/partial save doesn't
    // leave a stat at 0 (e.g. health=0 would read back as an instantly-dead pet).
    petReset();
    int8_t setupFlag = 0;
    nvs_get_i8(h, "setup", &setupFlag);
    if (setupFlag) pet.setupDone = true;
    char buf[16]; size_t sz = sizeof(buf);
    if (nvs_get_str(h, "name", buf, &sz) == ESP_OK)
        pet.name = String(buf);
    int8_t v8; int32_t v32;
    if (nvs_get_i8(h, "type", &v8) == ESP_OK) pet.type = (PetType)v8;
    if (nvs_get_i32(h, "hunger", &v32) == ESP_OK) pet.hunger = (uint8_t)v32;
    if (nvs_get_i32(h, "happy", &v32) == ESP_OK) pet.happiness = (uint8_t)v32;
    if (nvs_get_i32(h, "energy", &v32) == ESP_OK) pet.energy = (uint8_t)v32;
    if (nvs_get_i32(h, "health", &v32) == ESP_OK) pet.health = (uint8_t)v32;
    if (nvs_get_i32(h, "clean", &v32) == ESP_OK) pet.cleanliness = (uint8_t)v32;
    if (nvs_get_i32(h, "xp", &v32) == ESP_OK) pet.xp = (uint32_t)v32;
    if (nvs_get_i32(h, "xpneed", &v32) == ESP_OK) pet.xpToNext = (uint32_t)v32;
    if (nvs_get_i32(h, "level", &v32) == ESP_OK) pet.level = (uint8_t)v32;
    if (nvs_get_i32(h, "age", &v32) == ESP_OK) pet.ageDays = (uint16_t)v32;
    if (nvs_get_i32(h, "steps", &v32) == ESP_OK) pet.steps = (uint32_t)v32;
    if (nvs_get_i32(h, "dead", &v32) == ESP_OK) pet.dead = (bool)v32;
    if (nvs_get_i32(h, "sick", &v32) == ESP_OK) pet.sick = (bool)v32;
    if (nvs_get_i32(h, "sleep", &v32) == ESP_OK) pet.sleeping = (bool)v32;
    if (nvs_get_i32(h, "evolved", &v32) == ESP_OK) pet.evolved = (bool)v32;
    if (nvs_get_i32(h, "stage", &v32) == ESP_OK) pet.stage = (uint8_t)v32;
    if (nvs_get_i32(h, "incub", &v32) == ESP_OK) pet.incubationProgress = (uint8_t)v32;
    if (nvs_get_i32(h, "feedc", &v32) == ESP_OK) pet.feedCount = (uint16_t)v32;
    if (nvs_get_i32(h, "playc", &v32) == ESP_OK) pet.playCount = (uint16_t)v32;
    if (nvs_get_i32(h, "sleepc", &v32) == ESP_OK) pet.sleepCount = (uint16_t)v32;
    if (nvs_get_i32(h, "cleanc", &v32) == ESP_OK) pet.cleanCount = (uint16_t)v32;
    if (nvs_get_i32(h, "healed", &v32) == ESP_OK) pet.healedHp = (uint16_t)v32;
    if (nvs_get_i32(h, "bwon", &v32) == ESP_OK) pet.battlesWon = (uint16_t)v32;
    if (nvs_get_i32(h, "blost", &v32) == ESP_OK) pet.battlesLost = (uint16_t)v32;
    if (nvs_get_i32(h, "totxp", &v32) == ESP_OK) pet.totalXp = (uint32_t)v32;
    nvs_close(h);
    pet.lastUpdate = millis();
    return pet.setupDone;
}

// ======================================================
// NVS SAVE/LOAD
// ======================================================

// ======================================================
// ACTIONS
// ======================================================

static void doFeed()
{
    if (pet.dead || pet.sleeping) { beepError(); return; }
    bool wasFull = (pet.hunger >= PET_MAX_STAT);   // nothing to raise -> content heart
    petFeed();
    glowPixel(COLOR_FEED, 400);
    beepAction();
    uiSetEmote(wasFull ? EMOTION_HAPPY : EMOTE_FED);
    savePet();
}

static void doPlay()
{
    if (pet.dead || pet.sleeping) { beepError(); return; }
    if (pet.energy < 10) { beepError(); return; }
    bool wasFull = (pet.happiness >= PET_MAX_STAT);
    petPlay();
    glowPixel(COLOR_PLAY, 500);
    beepAction();
    uiSetEmote(wasFull ? EMOTION_HAPPY : EMOTE_GAME);
    savePet();
}

static void doSleep()
{
    if (pet.dead) { beepError(); return; }
    if (pet.sleeping) { petWakeUp(); savePet(); uiSetMenuIndex(2); return; }
    petSleep();
    glowPixel(COLOR_SLEEP, 500);
    playTone(400, 150); delay(80); playTone(350, 200);
    savePet();
}

static void doClean()
{
    if (pet.dead || pet.sleeping) { beepError(); return; }
    bool wasFull = (pet.cleanliness >= PET_MAX_STAT);
    petClean();
    glowPixel(COLOR_CLEAN, 400);
    beepAction();
    uiSetEmote(wasFull ? EMOTION_HAPPY : EMOTE_CLEAN);
    savePet();
}

static void doHeal()
{
    if (pet.dead || pet.sleeping) { beepError(); return; }
    bool wasFull = (pet.health >= PET_MAX_STAT);
    petHeal();
    glowPixel(COLOR_HEAL, 500);
    beepConfirm();
    uiSetEmote(wasFull ? EMOTION_HAPPY : EMOTE_HEALED);   // cross while healing, heart when already full
    savePet();
}

static void doEvolve()
{
    if (!petCanEvolve()) { beepError(); return; }

    uint32_t col = PET_AMBIENT_COLORS[pet.type];

    // Charge-up: rising arpeggio (C-E-G-C-E) with a glow pulse per note.
    static const uint16_t build[] = { 523, 659, 784, 1047, 1319 };
    for (uint8_t i = 0; i < 5; i++)
    {
        glowPixel(col, 130);
        playTone(build[i], 100);
        delay(40);
    }

    petEvolve();

    // Transform: bright burst, then a triumphant fanfare (G-C-E-G, last held) under a lit glow.
    glowAll(col, 700);
    ledFill(col);
    playTone(784, 150);  delay(30);
    playTone(1047, 150); delay(30);
    playTone(1319, 150); delay(30);
    playTone(1568, 420);
    pixels.fill(0); pixels.show();

    uiSetEmote(EMOTION_EXCITED);
    savePet();
}

static void doFactoryReset()
{
    battleStop();   // serial RESET can arrive mid-battle: kill scan/adverts BEFORE wiping
                    // state, or the orphaned scan runs forever and stats never broadcast again
    beepError();
    delay(300);
    beepError();
    glowPixel(COLOR_DEAD, 500);
    nvs_handle_t h;
    if (nvs_open("tamadata", NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    nvs_flash_erase();
    nvs_flash_init();
    petReset();
    updateAmbientColor();
    uiNameReset();
    uiSetSelectedType(0);
    uiSetScreen(SCREEN_SELECT_TYPE);
    glowAll(COLOR_HEAL, 1000);
    beepConfirm();
}

static void revivePet()
{
    pet.dead = false;
    pet.health = 50;
    pet.hunger = 50;
    pet.happiness = 50;
    pet.energy = 50;
    pet.sick = false;
    glowAll(COLOR_HEAL, 800);
    beepConfirm();
    savePet();
}

// ======================================================
// AGE / AUTO-SAVE
// ======================================================

static uint32_t lastAgeTick = 0;
static uint32_t lastAutoSave = 0;

static void updateTimers()
{
    uint32_t now = millis();

    if (now - lastAgeTick >= PET_AGE_INTERVAL)
    {
        lastAgeTick = now;
        if (pet.stage > 0) pet.ageDays++;
    }

    if (now - lastAutoSave >= PET_SAVE_INTERVAL)
    {
        lastAutoSave = now;
        savePet();
    }

    // Drive the bursty leaderboard broadcast: a short 3-4 packet burst every 10s
    // while idle, radio silent in between (saves battery + heat, no receiver flood).
    battleStatsTick();
}

// ======================================================
// INPUT HANDLERS
// ======================================================

static void handleSelectTypeScreen()
{
    if (uiInputLocked()) return;
    if (leftRelease())    { uiCycleType(-1); beepLeft(); ambientColor = PET_AMBIENT_COLORS[uiGetSelectedType()]; }
    if (rightRelease())  { uiCycleType(1);  beepRight(); ambientColor = PET_AMBIENT_COLORS[uiGetSelectedType()]; }
    if (okRelease())
    {
        beepSelect();
        pet.type = (PetType)uiGetSelectedType();
        updateAmbientColor();
        uiNameReset();
        uiSetScreen(SCREEN_ENTER_NAME);
    }
}

static void handleEnterNameScreen()
{
    if (uiInputLocked()) return;
    uint32_t now = millis();

    // UP - short click = prev letter, long hold = delete
    static uint32_t leftPressStart = 0;
    static bool leftRepeating = false;
    static uint32_t leftRepeatTime = 0;
    static bool prevLeftLocal = false;

    bool leftNow = btnLeftPressed();
    if (leftNow && !prevLeftLocal) {
        leftPressStart = now;
        leftRepeating = false;
    }
    if (leftNow && !leftRepeating && (now - leftPressStart >= BTN_LONG_PRESS_MS)) {
        if (uiNameLen() == 0) {
            consumeAllButtons();
            uiSetScreen(SCREEN_SELECT_TYPE);
            beepLeft();
            prevLeftLocal = false;
            leftRepeating = false;
            leftPressStart = 0;
            return;
        }
        uiNameDeleteChar();
        beepLeft();
        leftRepeating = true;
        leftRepeatTime = now;
    }
    if (leftNow && leftRepeating && (now - leftRepeatTime >= BTN_REPEAT_MS)) {
        uiNameDeleteChar();
        beepLeft();
        leftRepeatTime = now;
    }
    if (!leftNow && prevLeftLocal && !leftRepeating) {
        uiCharsetUp();
        beepLeft();
    }
    prevLeftLocal = leftNow;

    // DOWN - short click = next letter, long hold = SHIFT (add letter, inverted case)
    static uint32_t rightPressStart = 0;
    static bool rightShifted = false;
    static bool prevRightLocal = false;

    bool rightNow = btnRightPressed();
    if (rightNow && !prevRightLocal) {
        rightPressStart = now;
        rightShifted = false;
    }
    if (rightNow && !rightShifted && (now - rightPressStart >= BTN_LONG_PRESS_MS)) {
        uiNameAddCharShift(uiCharsetCurrent());   // one-shot: add letter with inverted case
        beepSelect();
        rightShifted = true;                       // blocks the "next letter" on release
    }
    if (!rightNow && prevRightLocal && !rightShifted) {
        uiCharsetDown();                          // short click = next letter
        beepRight();
    }
    prevRightLocal = rightNow;

    // OK - short press add char, long press confirm
    // Quick tap (< BTN_OK_CONFIRM_MS/2) adds a character.
    // Hold longer but release before the long-press threshold → nothing.
    // Hold >= BTN_OK_CONFIRM_MS → confirm name.
    static uint32_t okPressStart = 0;
    static bool okLongPressed = false;
    static bool prevOkLocal = false;

    bool okNow = btnOkPressed();
    if (okNow && !prevOkLocal) {
        okPressStart = now;
        okLongPressed = false;
    }
    if (okNow && !okLongPressed && (now - okPressStart >= BTN_OK_CONFIRM_MS)) {
        okLongPressed = true;
        uiSetScreen(SCREEN_CONFIRM_NAME);
        beepSelect();
    }
    if (!okNow && prevOkLocal && !okLongPressed && (now - okPressStart < BTN_OK_CONFIRM_MS / 2)) {
        beepSelect();
        uiNameAddChar(uiCharsetCurrent());
    }
    prevOkLocal = okNow;
}

static void handleConfirmNameScreen()
{
    if (uiInputLocked()) return;
    if (leftRelease())
    {
        uiSetScreen(SCREEN_ENTER_NAME);
        beepLeft();
    }
    if (rightRelease() || okRelease())
    {
        if (uiNameLen() == 0)
            pet.name = petDefaultName(pet.type);
        else
            pet.name = String(uiGetNameBuf());
        pet.level = 0;
        pet.xp = 0;
        pet.xpToNext = LEVEL_START_XP;
        pet.lastUpdate = millis();
        pet.setupDone = true;
        savePet();
        uiSetScreen(SCREEN_HATCHING);
        beepConfirm();
    }
}

static void handleMainScreen()
{
    if (uiInputLocked()) return;
    if (leftRelease())    { uiSetScreen(SCREEN_STATS); beepLeft(); }
    if (rightRelease())  { uiSetScreen(SCREEN_MENU); beepRight(); }
    if (okRelease())
    {
        if (pet.dead) { beepError(); return; }
        if (pet.sleeping) { beepError(); return; }

        uint8_t emote;
        bool raised = true;
        if (pet.hunger < 80)
        {
            pet.hunger = min(80, (int)pet.hunger + 20);
            pet.feedCount++;                 // quick feed counts toward stats
            emote = EMOTE_FED;
        }
        else if (pet.happiness < 60)
        {
            pet.happiness = min(60, (int)pet.happiness + 15);
            pet.playCount++;                 // quick play counts toward stats
            emote = EMOTE_GAME;
        }
        else if (pet.health < 90)
        {
            uint8_t before = pet.health;
            pet.health = min(90, (int)pet.health + 25);
            pet.healedHp += pet.health - before;   // quick heal counts toward stats
            emote = EMOTE_HEALED;
        }
        else { emote = EMOTION_HAPPY; raised = false; }  // nothing to raise -> content heart

        if (raised) { petGainXP(XP_QUICK); beepAction(); savePet(); }
        else        { beepSelect(); }
        uiSetEmote(emote);
    }
}

static void handleStatsScreen()
{
    if (uiInputLocked()) return;
    if (leftRelease())                    { uiSetScreen(SCREEN_STATS2); beepLeft(); }
    else if (rightRelease() || okRelease()) { uiSetScreen(SCREEN_MAIN); beepRight(); }
}

static void handleStats2Screen()
{
    if (uiInputLocked()) return;
    if (leftRelease())                    { uiSetScreen(SCREEN_STATS); beepLeft(); }
    else if (rightRelease() || okRelease()) { uiSetScreen(SCREEN_MAIN); beepRight(); }
}

static void handleMenuScreen()
{
    if (uiInputLocked()) return;

    static bool prevLeft = false, prevRight = false;
    static uint32_t lastMenuMove = 0;
    uint32_t now = millis();
    bool leftNow = btnLeftPressed();
    bool rightNow = btnRightPressed();

    if (leftNow && !prevLeft) { uiMenuUp(); beepLeft(); lastMenuMove = now; }
    if (rightNow && !prevRight) { uiMenuDown(); beepRight(); lastMenuMove = now; }

    if (leftNow && now - lastMenuMove > BTN_HOLD_MS)
    {
        uiMenuUp();
        lastMenuMove = now - (BTN_HOLD_MS - BTN_REPEAT_MS);
    }
    if (rightNow && now - lastMenuMove > BTN_HOLD_MS)
    {
        uiMenuDown();
        lastMenuMove = now - (BTN_HOLD_MS - BTN_REPEAT_MS);
    }

    prevLeft = leftNow;
    prevRight = rightNow;

    if (okRelease())
    {
        if (pet.sleeping)
        {
            doSleep();
            ledFill(pixels.Color(255, 210, 0));
            playTone(880, 100); delay(110);
            playTone(1100, 100); delay(110);
            playTone(880, 100); delay(110);
            playTone(1100, 200);
            uiSetScreen(SCREEN_MAIN);
            return;
        }
        int8_t idx = uiGetMenuIndex();
        int8_t action = menuActionIdx(idx);
        beepSelect();
        switch (action)
        {
            case 0: doFeed(); break;
            case 1: doPlay(); break;
            case 2: doSleep(); break;
            case 3: doClean(); break;
            case 4: doHeal(); break;
            case 5: doEvolve(); break;
            case 6:
                if (pet.health < 15) {
                    uiSetScreen(SCREEN_LOW_HP);
                    beepError();
                } else {
                    consumeAllButtons(); battleStartSearch();
                }
                break;
            case 7: break;   // Exit: ничего не делаем, общий хвост ниже ведёт на SCREEN_MAIN
        }
        if (action != 6) uiSetScreen(SCREEN_MAIN);
    }
}

static void handleConfirmResetScreen()
{
    if (uiInputLocked()) return;
    if (okRelease())
    {
        beepSelect();
        glowPixel(COLOR_DEAD, 400);
        doFactoryReset();
    }
    if (leftRelease() || rightRelease())
    {
        beepLeft();
        glowAll(0x00FF88, 400);
        uiSetScreen(screenBeforeConfirmReset);
    }
}

static void handleDeadScreen()
{
    if (uiInputLocked()) return;
    if (rightRelease()) { revivePet(); uiSetScreen(SCREEN_MAIN); }
    if (okRelease())   { } 
    if (leftRelease())   { screenBeforeConfirmReset = SCREEN_DEAD; uiSetScreen(SCREEN_CONFIRM_RESET); }
}

// ======================================================
// BOOT ANIMATION
// ======================================================

static void drawLogoRows(const uint8_t* bitmap, uint8_t bottomRows, uint16_t color)
{
    uint8_t bw = 16;
    for (uint8_t r = 0; r < bottomRows && r < 64; r++)
    {
        uint8_t row = 63 - r;
        for (uint8_t c = 0; c < bw; c++)
        {
            uint8_t byte = pgm_read_byte(bitmap + (uint16_t)row * bw + c);
            if (!byte) continue;
            for (uint8_t b = 0; b < 8; b++)
            {
                if (byte & (0x80 >> b))
                    display.drawPixel(c * 8 + b, row, color);
            }
        }
    }
}

static void showBootAnimation()
{
    uint32_t start = millis();
    uint32_t t;
    uint32_t lastTone = 0;
    uint8_t toneIdx = 0;

    while (true)
    {
        t = millis() - start;
        if (t >= UI_BOOTLOGO_TIME + 1000) break;

        display.clearDisplay();
        display.drawBitmap(0, 0, logo_1_128x64, 128, 64, BLACK);

        uint8_t completed;
        uint16_t logo3Color;

        if (t < UI_BOOTLOGO_TIME)
        {
            completed = (uint8_t)(t * 64 / UI_BOOTLOGO_TIME);
            logo3Color = ((t / 500) % 2 == 1) ? BLACK : WHITE;
        }
        else
        {
            completed = 64;
            logo3Color = WHITE;
        }

        drawLogoRows(logo_2_128x64, completed, WHITE);
        display.drawBitmap(0, 0, logo_3_128x64, 128, 64, logo3Color);

        ledFill(pixels.Color(80, 80, 80));

        if (t - lastTone >= 180)
        {
            lastTone = t;
            uint16_t freqs[] = {523, 659, 784, 1047, 659, 784, 1047, 1319};
            playTone(freqs[toneIdx & 7], 50);
            toneIdx++;
        }

        display.display();
        delay(16);
    }

    ledFill(pixels.Color(255, 255, 255));
    playTone(1976, 80);
    delay(100);
    playTone(2637, 200);
    pixels.fill(0);
    pixels.show();
}

static void showBootAnimationFinale()
{
    display.clearDisplay();
    display.drawBitmap(0, 0, logo_1_128x64, 128, 64, BLACK);
    drawLogoRows(logo_2_128x64, 64, WHITE);
    display.drawBitmap(0, 0, logo_3_128x64, 128, 64, WHITE);
    ledFill(pixels.Color(80, 80, 80));
    display.display();

    delay(300);

    ledFill(pixels.Color(255, 255, 255));
    playTone(1976, 80);
    delay(100);
    playTone(2637, 200);
    pixels.fill(0);
    pixels.show();
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.println("SYS | OFFGOTCHI ONLINE");

    // 160 -> 80 MHz: ~half the CPU-domain current draw. 80 MHz is the minimum the
    // radio needs and is plenty for the UI/game logic. Set before any peripheral
    // (NeoPixel RMT, buzzer LEDC) so they clock off the final frequency.
    setCpuFrequencyMhz(80);

    pixels.setPin(NEOPIXEL_PIN);
    pixels.begin();
    pixels.setBrightness(80);
    ledFill(0x00FF00);

    randomSeed(millis() ^ micros());

    nvs_flash_init();
    Serial.println("NVS | EEPROM ready");

    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_OK, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    Serial.println("HW  | INPUT ready");

    ledcSetup(0, 5000, 8);
    ledcAttachPin(BUZZER_PIN, 0);
    Serial.println("HW  | BUZZER ready");

    pixels.fill(0);
    pixels.setBrightness(NEOPIXEL_BRIGHTNESS);
    pixels.show();
    Serial.println("HW  | NEO ready");

    hasBooted = loadPet();
    updateAmbientColor();

    battlePreinit();
#if IDLE_SLEEP_ENABLE
    esp_bt_sleep_enable();   // best-effort: let the BLE controller modem-sleep so light sleep can engage
#endif

    uiInit();

    if (btnLeftPressed())
    {
        consumeAllButtons();
        if (hasBooted)
            screenBeforeConfirmReset = pet.dead ? SCREEN_DEAD : SCREEN_MAIN;
        else
            screenBeforeConfirmReset = SCREEN_SELECT_TYPE;
        uiSetScreen(SCREEN_CONFIRM_RESET);
        glowPixel(COLOR_DEAD, 500);
        beepError();
    }
    else
    {
        if (btnRightPressed())
        {
            consumeAllButtons();
            showBootAnimationFinale();
        }
        else
        {
            showBootAnimation();
        }

        if (!hasBooted)
        {
        uiSetSelectedType((uint8_t)pet.type);
        uiNameReset();
        uiSetScreen(SCREEN_WELCOME);
        glowAll(0xFFFFFF, 600);
        playTone(880, 50); delay(40); playTone(1100, 50); delay(40); playTone(1320, 80);
    }
    else if (pet.stage < 3)
    {
        uiSetScreen(SCREEN_HATCHING);
        glowAll(0x00CCFF, 600);
        playTone(660, 50); delay(40); playTone(880, 50); delay(40); playTone(1100, 80);
    }
    else
    {
        uiSetScreen(pet.dead ? SCREEN_DEAD : SCREEN_MAIN);
        glowAll(0x00FF88, 600);
        playTone(880, 50); delay(40); playTone(1100, 50); delay(40); playTone(1320, 80);
    }
    }

    lastAgeTick = millis();
    lastAutoSave = millis();

    
    Serial.println("SYS | OFFGOTCHI STARTED");
    Serial.println("SYS | Type HELP for available commands");
}

// ======================================================
// EMOTION -> EMOTE MAPPING
// ======================================================

// Translate the pet's internal emotion into a display emote code. Sets *needs = true
// for "wants care" states (hungry/dirty/bored/tired), which also get a chirp and a
// sick face. Returns EMOTE_NONE when there is nothing to show.
static uint8_t emoteForEmotion(uint8_t emotion, bool* needs)
{
    *needs = false;
    switch (emotion)
    {
        case EMOTION_HUNGRY: *needs = true; return EMOTE_NEED_FOOD;
        case EMOTION_DIRTY:  *needs = true; return EMOTE_NEED_CLEAN;
        case EMOTION_BORED:  *needs = true; return EMOTE_NEED_PLAY;
        case EMOTION_SLEEPY: *needs = true; return EMOTE_NEED_ENERGY;  // tired while awake
        case EMOTION_SICK:   return EMOTION_SICK;   // health: existing icon/face, no new chirp
        // Note: no ambient heart for EMOTION_HAPPY — the heart is action feedback only
        // (shown when a care target is already full), so it can't stomp an action emote.
        default:             return EMOTE_NONE;
    }
}

// ======================================================
// IDLE LIGHT SLEEP (power saving)
// ======================================================
#if IDLE_SLEEP_ENABLE
// Quiescent enough to light-sleep between BLE bursts: idle for ACTIVITY_TIMEOUT,
// not in a battle, and on a screen that's fine to freeze. The screen is off during
// idle sleep anyway, so auto-emotes don't need to keep us awake.
static bool deepIdle()
{
    if (!uiIdle) return false;
    if (!battleIsIdle()) return false;
    switch (uiGetScreen())
    {
        case SCREEN_MAIN:
        case SCREEN_STATS:
        case SCREEN_STATS2:
        case SCREEN_MENU:
        case SCREEN_DEAD:
            return true;
        default:
            return false;
    }
}

// Latch the display's control lines so the ST7567 keeps its image (and isn't
// reset) while the chip light-sleeps. The transflective LCD stays perfectly
// readable with no backlight, so the pet remains on screen during sleep.
static void holdDisplayPins(bool hold)
{
    const gpio_num_t pins[5] = {
        (gpio_num_t)OLED_RST, (gpio_num_t)OLED_CS, (gpio_num_t)OLED_DC,
        (gpio_num_t)OLED_SCK, (gpio_num_t)OLED_MOSI
    };
    for (int i = 0; i < 5; i++)
        if (hold) gpio_hold_en(pins[i]); else gpio_hold_dis(pins[i]);
}

// Light-sleep until the next redraw/burst is due or any button is pressed. The
// whole chip (CPU + radio) drops to ~1 mA for the window; millis() keeps counting
// from the RTC, so every game timer stays correct on wake. Display pins are held
// so the LCD keeps showing the pet the whole time. If light sleep doesn't actually
// engage (e.g. the BLE controller blocks it), we fall back to a normal delay.
static void idleLightSleep()
{
    uint32_t budget = battleStatsSleepBudgetMs();

    // Never sleep past the next pet-decay tick, so petUpdate keeps firing on its
    // normal PET_UPDATE_RATE cadence -> game balance is unchanged by sleeping.
    uint32_t sinceTick = millis() - pet.lastUpdate;
    uint32_t petBudget = (sinceTick >= PET_UPDATE_RATE) ? 0 : (PET_UPDATE_RATE - sinceTick);
    if (petBudget < budget) budget = petBudget;

    if (budget < 200) { delay(20); return; }   // a burst or tick is due now: loop fast to service it
    if (budget > IDLE_SLEEP_MAX_MS) budget = IDLE_SLEEP_MAX_MS;

    // Wake sources: timer (next burst) + any button going LOW (buttons are active-low).
    esp_sleep_enable_timer_wakeup((uint64_t)budget * 1000ULL);
    gpio_wakeup_enable((gpio_num_t)BTN_LEFT,  GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN_OK,    GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN_RIGHT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    ledcWrite(0, 0);                   // silence buzzer before sleep
    ledcDetachPin(BUZZER_PIN);         // disconnect LEDC so it can't glitch the MOSFET
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);     // explicitly hold MOSFET off
    gpio_hold_en((gpio_num_t)BUZZER_PIN);

    holdDisplayPins(true);             // freeze LCD lines so it keeps the image through sleep
    uint32_t t0 = millis();
    esp_err_t rc = esp_light_sleep_start();
    uint32_t slept = millis() - t0;
    holdDisplayPins(false);            // release so the next uiUpdate can redraw

    gpio_hold_dis((gpio_num_t)BUZZER_PIN);
    ledcAttachPin(BUZZER_PIN, 0);      // re-attach buzzer to LEDC after wake
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    gpio_wakeup_disable((gpio_num_t)BTN_LEFT);
    gpio_wakeup_disable((gpio_num_t)BTN_OK);
    gpio_wakeup_disable((gpio_num_t)BTN_RIGHT);

    // The panel kept its image (pins were held); the loop redraws it each wake
    // (~1 fps). A button press additionally returns us to full interactivity.
    if (cause == ESP_SLEEP_WAKEUP_GPIO)
    {
        consumeAllButtons();           // the wake press only wakes; it doesn't fire an action
        lastActivityTime = millis();
    }

    // First few sleeps: report to serial so it's easy to confirm sleep really engages.
    static uint8_t dbg = 0;
    if (dbg < 8) {
        Serial.printf("[sleep] budget=%lu slept=%lu cause=%d rc=%d\n",
                      (unsigned long)budget, (unsigned long)slept, (int)cause, (int)rc);
        dbg++;
    }

    // Safety net: if it returned without really sleeping, don't busy-spin.
    if (slept < 100 && budget >= 200) delay(uiIdle ? 45 : 33);
}
#endif // IDLE_SLEEP_ENABLE

// ======================================================
// SERIAL COMMAND PARSER
// ======================================================

static void handleSerialCommands()
{
    static String buf;
    while (Serial.available())
    {
        char c = Serial.read();
        if (c == '\n')
        {
            buf.trim();
            if (buf.length() == 0) { buf = ""; continue; }

            String uc = buf;
            uc.toUpperCase();

            if (uc == "HELP")
            {
                Serial.println(F("SYS | OFFGOTCHI COMMANDS"));
                Serial.println(F("HELP        Show this help"));
                Serial.println(F("STAT        Show pet stats"));
                Serial.println(F("NAME <name> Set pet name (A-Z a-z 0-9 special, max 8 chars)"));
                Serial.println(F("INFO        Show leaderboard URL"));
                Serial.println(F("RESET       Factory reset pet (confirms at addon)"));
            }
            else if (uc == "RESET")
            {
                Serial.println(F("SYS | Confirm reset on device..."));
                Serial.flush();
                screenBeforeConfirmReset = uiGetScreen();
                uiSetScreen(SCREEN_CONFIRM_RESET);
            }
            else if (uc == "INFO")
            {
                Serial.println(F("SYS | Visit https://rottenmechanism.com/offzone26/ to view leaderboard statistics"));
            }
            else if (uc == "STAT")
            {
                Serial.printf("SYS | PET STATS\n");
                Serial.printf("SYS | Name:    %s\n", pet.name.c_str());
                Serial.printf("SYS | Team:    %d\n", (int)pet.type);
                Serial.printf("SYS | Level:   %u\n", pet.level);
                Serial.printf("SYS | XP:      %lu / %lu\n", (unsigned long)pet.xp, (unsigned long)pet.xpToNext);
                Serial.printf("SYS | Hunger:  %u\n", pet.hunger);
                Serial.printf("SYS | Happy:   %u\n", pet.happiness);
                Serial.printf("SYS | Energy:  %u\n", pet.energy);
                Serial.printf("SYS | Health:  %u\n", pet.health);
                Serial.printf("SYS | Clean:   %u\n", pet.cleanliness);
                Serial.printf("SYS | Age:     %u\n", pet.ageDays);
                Serial.printf("SYS | Dead:    %s\n", pet.dead ? "yes" : "no");
                Serial.printf("SYS | Sleep:   %s\n", pet.sleeping ? "yes" : "no");
                Serial.printf("SYS | Evolved: %s\n", pet.evolved ? "yes" : "no");
            }
            else if (uc.startsWith("NAME"))
            {
                if (uc == "NAME" || buf.length() <= 5)
                {
                    Serial.printf("SYS | Name: %s\n", pet.name.c_str());
                }
                else
                {
                    String name = buf.substring(5);
                    name.trim();
                    bool valid = name.length() > 0 && name.length() <= 8;
                    if (valid)
                    {
                        for (int i = 0; i < name.length(); i++)
                        {
                            char ch = name.charAt(i);
                            // stats.csv is comma-separated and fetched raw by the leaderboard
                            // page — a ',' or '"' in the name shifts columns for EVERY device
                            if (ch == ',' || ch == '"')
                            {
                                valid = false;
                                break;
                            }
                            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                  (ch >= '0' && ch <= '9') || (ch >= 32 && ch <= 47) ||
                                  (ch >= 58 && ch <= 64) || (ch >= 91 && ch <= 96) ||
                                  (ch >= 123 && ch <= 126)))
                            {
                                valid = false;
                                break;
                            }
                        }
                    }
                    if (valid)
                    {
                        if (pet.name != name)
                        {
                            pet.name = name;
                            Serial.flush();
                            savePetName();
                            delay(10);
                            Serial.printf("NVS | Name set to: %s\n", pet.name.c_str());
                        }
                        else
                        {
                            Serial.printf("SYS | Name unchanged: %s\n", pet.name.c_str());
                        }
                    }
                    else
                    {
                        Serial.println(F("SYS | Name must be Latin letters, digits & special chars (1-8)"));
                    }
                }
            }
            else
            {
                Serial.println(F("SYS | Unknown command. Type HELP for available commands."));
            }

            buf = "";
            Serial.flush();
            while (Serial.available()) Serial.read();
        }
        else if (c != '\r')
        {
            buf += c;
            if (buf.length() > 64) buf = "";   // garbage without a newline must not grow the heap forever
        }
    }
}

// ======================================================
// LOOP
// ======================================================

void loop()
{
    handleSerialCommands();
    // activity tracking from button presses
    static bool prevAnyPressed = false;
    static bool glowWakeSkip = false;
    bool anyPressed = btnLeftPressed() || btnOkPressed() || btnRightPressed();
    if (anyPressed && !prevAnyPressed)
    {
        if (millis() - lastActivityTime >= ACTIVITY_TIMEOUT)
            glowWakeSkip = true;
        lastActivityTime = millis();
    }
    prevAnyPressed = anyPressed;

    bool wasSleeping = pet.sleeping;
    static uint8_t prevHealth = pet.health;
    petUpdate();
    if (pet.sleeping && !wasSleeping)
        beepSelect();
    animationUpdate();
    updateTimers();

    // emotion change → emote bubble (+ chirp when the pet needs care)
    static uint8_t prevEmotion = EMOTE_NONE;
    uint8_t curEmotion = pet.dead ? EMOTE_NONE : (uint8_t)pet.emotion;
    if (curEmotion != prevEmotion)
    {
        prevEmotion = curEmotion;
        if (!pet.dead && !pet.sleeping && curEmotion != EMOTION_IDLE)
        {
            bool needs = false;
            uint8_t code = emoteForEmotion(curEmotion, &needs);
            if (code != EMOTE_NONE)
            {
                uiSetEmote(code);
                if (needs) beepNeed();
            }
        }
    }

    // level up → yellow flash + cheerful beep + arrow emote
    if (pet.leveledUp)
    {
        pet.leveledUp = false;
        if (!pet.dead && !pet.sleeping)
        {
            glowPixel(0xFFFF00, 250);
            playTone(800, 40); delay(30);
            playTone(1000, 40); delay(30);
            playTone(1200, 60);
            uiSetEmote(EMOTE_UP_ARROW);
        }
    }

    if (!pet.dead && pet.health < prevHealth && pet.health < 10)
    {
        Screen cs = uiGetScreen();
        if (cs == SCREEN_MAIN || cs == SCREEN_STATS || cs == SCREEN_STATS2)
        {
            if (pet.type == CUB3 && pet.evolved) ledFill(pixels.Color(150, 150, 150));
            else ledFill(pixels.Color(255, 0, 0));
            uiSetEmote(EMOTE_DEAD);
            playTone(200, 500);
        }
    }
    prevHealth = pet.health;

    Screen s = uiGetScreen();

    if (s == SCREEN_MAIN) uiUpdateWalking(millis());

    if (pet.dead && s != SCREEN_DEAD && s != SCREEN_SELECT_TYPE && s != SCREEN_ENTER_NAME && s != SCREEN_HATCHING && s != SCREEN_CONFIRM_RESET && s != SCREEN_CONFIRM_NAME && s != SCREEN_WELCOME)
    {
        consumeAllButtons();
        uiSetScreen(SCREEN_DEAD);
        glowPixel(COLOR_DEAD, 800);
        beepDead();
    }

    if (glowWakeSkip)
    {
        glowWakeSkip = false;
        consumeAllButtons();
    }
    else switch (s)
    {
        case SCREEN_SELECT_TYPE: handleSelectTypeScreen(); break;
        case SCREEN_ENTER_NAME:  handleEnterNameScreen(); break;
        case SCREEN_MAIN:        handleMainScreen(); break;
        case SCREEN_STATS:       handleStatsScreen(); break;
        case SCREEN_STATS2:      handleStats2Screen(); break;
        case SCREEN_MENU:        handleMenuScreen(); break;
        case SCREEN_DEAD:        handleDeadScreen(); break;
        case SCREEN_CONFIRM_RESET: handleConfirmResetScreen(); break;
        case SCREEN_CONFIRM_NAME:  handleConfirmNameScreen(); break;
        case SCREEN_BATTLE_SEARCH:
            if (!uiInputLocked() && leftRelease())
            {
                battleStop();
                uiSetScreen(SCREEN_MAIN);
            }
            battleUpdate();
            break;
        case SCREEN_BATTLE:        battleUpdate(); break;
        case SCREEN_BATTLE_WIN:
        case SCREEN_BATTLE_LOSE:
        case SCREEN_BATTLE_DRAW:
            if (leftRelease() || okRelease() || rightRelease())
            {
                battlePersistResult();
                petGainXP(battleGetRewardXP());
                savePet();
                battleStop();
                uiSetScreen(SCREEN_MAIN);
            }
            break;
        case SCREEN_LOW_HP:
            if (leftRelease() || rightRelease() || okRelease())
            {
                uiSetScreen(SCREEN_MAIN);
                beepLeft();
            }
            break;
        case SCREEN_WELCOME:
        {
            static uint32_t welcomeStart = 0;
            if (welcomeStart == 0) welcomeStart = millis();
            if (millis() - welcomeStart >= 4000)
            {
                welcomeStart = 0;
                uiSetScreen(SCREEN_SELECT_TYPE);
                beepSelect();
            }
            break;
        }
        case SCREEN_HATCHING:
        {
            uint32_t now = millis();
            static uint32_t lastTick = 0;
            static uint32_t animStart = 0;
            if (lastTick == 0) lastTick = now;
            uint32_t delta = now - lastTick;
            lastTick = now;

            if (pet.stage == 0)
            {
                static uint32_t accum = 0;
                accum += delta;
                uint32_t step = INCUBATOR_TIME / 100;
                if (step > 0 && accum >= step)
                {
                    accum -= step;
                    if (pet.incubationProgress < 100)
                        pet.incubationProgress++;
                        savePet();
                }
                if (pet.incubationProgress >= 100)
                {
                    pet.stage = 1;
                    savePet();
                    animStart = 0;
                }
            }

            if (pet.stage == 1)
            {
                static bool prevAnyHatch = false;
                bool anyNow = btnLeftPressed() || btnOkPressed() || btnRightPressed();
                if (!anyNow && prevAnyHatch)
                {
                    beepSelect();
                    uint8_t cnt = uiGetHatchPressCount() + 1;
                    uiSetHatchPressCount(cnt);
                    if (cnt >= 3)
                    {
                        pet.stage = 2;
                        savePet();
                        animStart = now;
                        uiSetHatchPressCount(0);
                    }
                }
                prevAnyHatch = anyNow;

                static uint32_t lastReadyBeep = 0;
                if (now - lastReadyBeep >= 5000)
                {
                    lastReadyBeep = now;
                    uiSetReadyFlash();
                    beepSelect();
                    glowPixel(COLOR_FEED, 200);
                }
            }

            if (pet.stage == 2)
            {
                if (animStart == 0) animStart = now;

                // --- Hatch melody: gentle rising pentatonic sparkle, one note per slot ---
                static const uint16_t hatchMelody[] =
                    { 523, 659, 784, 880, 1047, 1319, 1047, 1047 };  // C5 E5 G5 A5 C6 E6 C6 C6
                const uint8_t  melodyLen = 8;
                const uint16_t noteSlot  = 340;   // ms between note starts
                const uint16_t toneMs    = 200;   // tone-on time per note
                const uint16_t animMs    = 3000;  // total stage-2 length
                const uint16_t glowMs    = 1800;  // slow breathing period (was a 300ms toggle)

                static uint32_t lastNote = 0;
                static uint8_t  noteIdx = 0;
                if (now - lastNote >= noteSlot)
                {
                    lastNote = now;
                    if (noteIdx < melodyLen)
                        playTone(hatchMelody[noteIdx++], toneMs);
                }

                // Slow, smooth breathing glow. Fresh millis() so blocking tones don't freeze it.
                uint32_t gEl = millis() - animStart;
                float ph = (float)(gEl % glowMs) / (float)glowMs;
                uint8_t br = (uint8_t)(70 + 160 * (0.5f + 0.5f * sinf(ph * 6.2832f)));
                ledFill(((uint32_t)br << 16) | ((uint32_t)br << 8) | (uint32_t)br);

                if (millis() - animStart >= animMs)
                {
                    pet.stage = 3;
                    pet.level = 1;
                    pet.xp = 0;
                    pet.xpToNext = LEVEL_START_XP;
                    pet.lastUpdate = now;
                    savePet();
                    lastTick = 0;
                    animStart = 0;
                    lastNote = 0;
                    noteIdx = 0;
                    pixels.fill(0); pixels.show();
                    glowAll(0x00FF88, 600);
                    // "Hatched!" flourish — triumphant rising triad (C6-E6-G6)
                    playTone(1047, 120); delay(30);
                    playTone(1319, 120); delay(30);
                    playTone(1568, 340);
                    uiSetScreen(SCREEN_MAIN);
                }
            }
            break;
        }
        default:                 break;
    }

    uiUpdate();

    uiIdle = (millis() - lastActivityTime >= ACTIVITY_TIMEOUT);

    // ambient glow or off based on activity
    if (battleEnemyHitFlash())
        ledFill(pixels.Color(255, 255, 255));
    else if (!uiIdle)
        setAmbientGlow();
    else
        clearLeds();

    // Frame pacing. When deeply idle, light-sleep between BLE bursts (~1 mA) and
    // wake instantly on any button. Otherwise run at FPS_LOCK (config.h).
#if IDLE_SLEEP_ENABLE
    if (deepIdle())
        idleLightSleep();
    else
        delay(1000 / FPS_LOCK);
#else
    delay(1000 / FPS_LOCK);
#endif
}
