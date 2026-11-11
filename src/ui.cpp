#include "ui.h"
#include <ST7567.h>
#include "config.h"
#include "pet.h"
#include "sprites.h"
#include "animation.h"
#include "battle.h"

ST7567 display(OLED_WIDTH, OLED_HEIGHT, OLED_MOSI, OLED_SCK, OLED_DC, OLED_RST, OLED_CS);


static Screen currentScreen = SCREEN_MAIN;
static int8_t menuIndex = 0;

uint32_t lastScreenChange = 0;

static const char* menuLabels[8] =
{
    "Feed", "Play", "Sleep", "Clean", "Heal", "Evolve", "Battle", "Exit"
};

// menuIdx -> action: 0..4 общие (Feed..Heal), дальше зависит от состояния пета:
//   evolved:   5=Battle, 6=Exit   (Evolve уже недоступен)
//   canEvolve: 5=Evolve, 6=Exit   (Battle ещё недоступен)
//   otherwise: 5=Exit
int8_t menuActionIdx(int8_t menuIdx)
{
    if (menuIdx <= 4) return menuIdx;
    if (pet.evolved) return menuIdx + 1;                  // 5 -> Battle(6), 6 -> Exit(7)
    if (petCanEvolve()) return (menuIdx == 5) ? 5 : 7;    // 5 -> Evolve(5), 6 -> Exit(7)
    return 7;                                             // 5 -> Exit(7)
}

static int8_t menuCount()
{
    if (pet.sleeping) return 1;
    if (pet.evolved) return 7;
    if (petCanEvolve()) return 7;
    return 6;
}

Screen uiGetScreen() { return currentScreen; }
void uiSetScreen(Screen s) { currentScreen = s; lastScreenChange = millis(); }

bool uiInputLocked() { return millis() - lastScreenChange < INPUT_LOCK_MS; }

// ---- TYPE SELECTION BOOT STATE ----

static uint8_t selectedType = 0;
static const char* typeLabels[PET_TYPE_COUNT] =
    { PET1_DEFAULT_NAME, PET2_DEFAULT_NAME, PET3_DEFAULT_NAME };

uint8_t uiGetSelectedType() { return selectedType; }
void uiSetSelectedType(uint8_t t) { selectedType = t; }
void uiCycleType(int8_t dir)
{
    int8_t t = (int8_t)selectedType + dir;
    if (t < 0) t = PET_TYPE_COUNT - 1;
    if (t >= PET_TYPE_COUNT) t = 0;
    selectedType = (uint8_t)t;
}

// ---- NAME ENTRY STATE ----

static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const int8_t CHARSET_LEN = 26;

static int8_t charsetPos = 0;
static char nameBuf[PET_MAX_NAME + 1];
static uint8_t nameLen = 0;

const char* uiGetNameBuf() { return nameBuf; }
uint8_t uiNameLen() { return nameLen; }
void uiNameReset() { nameLen = 0; nameBuf[0] = 0; charsetPos = 0; }
// Append a letter, fixing its case. Default rule: first letter UPPER, rest lower.
// SHIFT (long press) inverts it: first letter lower, the rest UPPER.
static void nameAppend(char c, bool shift)
{
    if (nameLen >= PET_MAX_NAME) return;
    bool wantUpper = (nameLen == 0) ? !shift : shift;
    if (wantUpper) { if (c >= 'a' && c <= 'z') c -= 32; }   // -> uppercase
    else           { if (c >= 'A' && c <= 'Z') c += 32; }   // -> lowercase
    nameBuf[nameLen++] = c;
    nameBuf[nameLen] = 0;
}
void uiNameAddChar(char c)      { nameAppend(c, false); }
void uiNameAddCharShift(char c) { nameAppend(c, true);  }
void uiNameDeleteChar()
{
    if (nameLen == 0) return;
    nameLen--;
    nameBuf[nameLen] = 0;
}

void uiCharsetUp()
{
    charsetPos--;
    if (charsetPos < 0) charsetPos = CHARSET_LEN - 1;
}
void uiCharsetDown()
{
    charsetPos++;
    if (charsetPos >= CHARSET_LEN) charsetPos = 0;
}
char uiCharsetCurrent()
{
    return charset[charsetPos];
}

// ---- UTILITY ----

void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t val)
{
    if (w < 3 || h < 3) return;
    display.drawRect(x, y, w, h, BLACK);
    if (val > 100) val = 100;
    uint8_t fill = (uint8_t)((uint16_t)(w - 2) * val / 100);
    if (fill > 0)
        display.fillRect(x + 1, y + 1, fill, h - 2, BLACK);
}

#define SPRITE_W BODY_W
#define SPRITE_H BODY_H

// ---- SPRITE WALKING + EMOTE BUBBLE ----

static int16_t walkX = 48;
static int16_t walkY = 12;
static int8_t walkDirX = 1;
static int8_t walkDirY = 1;
static int16_t walkTargetX, walkTargetY;
static bool walking = false;
static uint32_t walkPhaseStart = 0;
static uint8_t walkFrame = 1;
static uint32_t lastStepTime = 0;
static uint8_t bodyVariant = 0;
static uint8_t currentEmote = EMOTE_NONE;
static uint8_t hatchPressCount = 0;
void uiSetHatchPressCount(uint8_t count) { hatchPressCount = count; }
uint8_t uiGetHatchPressCount() { return hatchPressCount; }

static uint32_t readyFlashEnd = 0;
void uiSetReadyFlash() { readyFlashEnd = millis() + 800; }
static uint32_t emoteStartTime = 0;
static const uint32_t EMOTE_DURATION = 3000;
bool uiIdle = false;

#define WALK_Y_MIN 11
#define WALK_Y_MAX 13
#define WALK_STEP 8
#define WALK_INTERVAL 300

void uiUpdateWalking(uint32_t now)
{
    if (pet.sleeping) return;
    uint32_t elapsed = now - walkPhaseStart;

    if (!walking)
    {
        if (elapsed < (uint32_t)random(WALK_PAUSE_MIN, WALK_PAUSE_MAX + 1)) return;

        walkDirX = random(0, 2) ? 1 : -1;
        int16_t distX = random(WALK_DIST_MIN, WALK_DIST_MAX + 1);
        walkTargetX = walkX + walkDirX * distX;
        if (walkTargetX < 0) walkTargetX = 0;
        if (walkTargetX > 128 - SPRITE_W) walkTargetX = 128 - SPRITE_W;

        walkDirY = random(0, 2) ? 1 : -1;
        int16_t distY = random(WALK_DIST_MIN / 2, WALK_DIST_MAX / 2 + 1);
        walkTargetY = walkY + walkDirY * distY;
        if (walkTargetY < WALK_Y_MIN) walkTargetY = WALK_Y_MIN;
        if (walkTargetY > WALK_Y_MAX) walkTargetY = WALK_Y_MAX;

        bodyVariant = (uint8_t)random(0, 2);
        walking = true;
        walkPhaseStart = now;
        lastStepTime = now;
    }
    else
    {
        if (walkX == walkTargetX && walkY == walkTargetY)
        { walking = false; walkPhaseStart = now; return; }

        if (now - lastStepTime < WALK_INTERVAL) return;
        lastStepTime = now;

        if (walkX != walkTargetX)
        {
            int16_t dx = walkTargetX - walkX;
            walkX += (dx > 0 ? WALK_STEP : -WALK_STEP);
            if ((dx > 0 && walkX > walkTargetX) || (dx < 0 && walkX < walkTargetX))
                walkX = walkTargetX;
            if (walkX < 0) walkX = 0;
            if (walkX > 128 - SPRITE_W) walkX = 128 - SPRITE_W;
        }
        if (walkY != walkTargetY)
        {
            int16_t dy = walkTargetY - walkY;
            int16_t stepY = max(WALK_STEP / 2, abs(dy));
            walkY += (dy > 0 ? stepY : -stepY);
            if ((dy > 0 && walkY > walkTargetY) || (dy < 0 && walkY < walkTargetY))
                walkY = walkTargetY;
            if (walkY < WALK_Y_MIN) walkY = WALK_Y_MIN;
            if (walkY > WALK_Y_MAX) walkY = WALK_Y_MAX;
        }

        if (walkFrame == 0) walkFrame = 1;
        else if (walkFrame == 2) walkFrame = 1;
        else walkFrame = (walkDirX == 1) ? 2 : 0;
    }
}

void uiSetEmote(uint8_t emote)
{
    currentEmote = emote;
    emoteStartTime = millis();
}

uint8_t uiGetCurrentEmote()
{
    if (millis() - emoteStartTime >= EMOTE_DURATION)
        currentEmote = EMOTE_NONE;
    return currentEmote;
}

// ---- EMOTE ICONS (15x15 from sprites.cpp) ----

static const uint8_t* emoteIcons[] = {
    0,                          // 0  EMOTION_IDLE / EMOTE_NONE
    emoteHeart_15x15,           // 1  EMOTION_HAPPY
    emoteHungry_15x15,          // 2  EMOTION_HUNGRY
    emoteSleep_15x15,           // 3  EMOTION_SLEEPY
    emoteSick_15x15,            // 4  EMOTION_SICK
    emoteSad_15x15,             // 5  EMOTION_SAD
    emoteHappy_15x15,           // 6  EMOTION_EXCITED
    emoteArrowUp_15x15,         // 7  EMOTE_UP_ARROW
    emoteGame_15x15,            // 8  EMOTE_GAME
    emoteClean_15x15,           // 9  EMOTE_CLEAN
    emoteDead_15x15,            // 10 EMOTE_DEAD
    emoteHungry_15x15,          // 11 EMOTE_FED
    emoteHungry_15x15,          // 12 EMOTE_NEED_FOOD
    emoteClean_15x15,           // 13 EMOTE_NEED_CLEAN
    emoteGame_15x15,            // 14 EMOTE_NEED_PLAY
    emoteSleep_15x15,           // 15 EMOTE_NEED_ENERGY
    emoteSick_15x15             // 16 EMOTE_HEALED      
};

static void drawEmoteBubble(int16_t spriteX)
{
    if (currentEmote == EMOTE_NONE) return;
    if (millis() - emoteStartTime >= EMOTE_DURATION) { currentEmote = EMOTE_NONE; return; }
    if (currentEmote >= (sizeof(emoteIcons) / sizeof(emoteIcons[0]))) return;

    const uint8_t* icon = emoteIcons[currentEmote];
    if (!icon) return;

    int16_t bx, by;
    int16_t bw = 21, bh = 21;
    int16_t spriteW = SPRITE_W;

    int8_t closer = !pet.evolved ? 5 : 0;
    if (spriteX + spriteW / 2 < 64)
        bx = spriteX + spriteW - closer;
    else
        bx = spriteX - bw + closer;

    if (bx < 0) bx = 0;
    if (bx + bw > 128) bx = 128 - bw;

    by = 12;

    display.fillRect(bx + 1, by + 3, 19, 19, WHITE);
    display.drawBitmap(bx + 3, by + 5, icon, 15, 15, BLACK);
}

void drawPetSprite(int16_t x, int16_t y, uint8_t type, uint8_t faceIdx, uint8_t frame, bool mirrored, bool forceAdult, uint8_t forceVariant)
{
    (void)frame;
    uint8_t v = (forceVariant < 2) ? forceVariant : bodyVariant;
    const uint8_t* mask = mirrored ? getMirroredMask(type, v, forceAdult) : getBodyMask(type, v, forceAdult);
    display.drawBitmap(x, y, mask, BODY_W, BODY_H, WHITE);
    const uint8_t* body = mirrored ? getMirroredBody(type, v, forceAdult) : getBodySprite(type, v, forceAdult);
    display.drawBitmap(x, y, body, BODY_W, BODY_H, BLACK);
    int16_t eyeX = x + (mirrored ? BODY_W - EYE_OX - EYE_W : EYE_OX);
    const uint8_t* eye = mirrored ? getMirroredEye(faceIdx, type, forceAdult) : getEyeSprite(faceIdx, type, forceAdult);
    display.drawBitmap(eyeX, y + EYE_OY, eye, EYE_W, EYE_H, BLACK);
}

static void drawSleepAnimation()
{
    if (!pet.sleeping) return;

    uint32_t t = millis() % 2800;
    uint8_t phase = t / 700;
    if (phase == 0) return;

    uint8_t spriteW = SPRITE_W;
    bool left = (walkX + spriteW / 2 < 64);

    if (phase >= 1)
    {
        int16_t sx = left ? walkX + spriteW - 3 : walkX - 7;
        int16_t sy = walkY + 8;
        display.drawBitmap(sx, sy, z1_mask_10x14, ZZZ_W, ZZZ_H, WHITE);
        display.drawBitmap(sx, sy, z1_10x14, ZZZ_W, ZZZ_H, BLACK);
    }
    if (phase >= 2)
    {
        int16_t sx = left ? walkX + spriteW + 3 : walkX - 14;
        int16_t sy = walkY + 4;
        display.drawBitmap(sx, sy, z2_mask_10x14, ZZZ_W, ZZZ_H, WHITE);
        display.drawBitmap(sx, sy, z2_10x14, ZZZ_W, ZZZ_H, BLACK);
    }
    if (phase >= 3)
    {
        int16_t sx = left ? walkX + spriteW + 11 : walkX - 22;
        int16_t sy = walkY - 1;
        display.drawBitmap(sx, sy, z3_mask_10x14, ZZZ_W, ZZZ_H, WHITE);
        display.drawBitmap(sx, sy, z3_10x14, ZZZ_W, ZZZ_H, BLACK);
    }
}

// ---- PET HEADER (name + level + XP bar) ----

static void drawPetHeader()
{
    display.setTextSize(1);
    display.setTextColor(BLACK);
    // No String concat here: this runs every frame at 24 fps — String temporaries
    // churn the heap and fragment it over multi-day uptime. Static buffer, zero allocs.
    static char prefix[24];
    snprintf(prefix, sizeof(prefix), "%s Lv.%u", pet.name.c_str(), (unsigned)pet.level);
    display.setCursor(0, 0);
    display.print(prefix);

    bool canEvolve = pet.level >= EVOLUTION_LEVEL && !pet.evolved;
    int16_t tw = (int16_t)strlen(prefix) * 6;

    if (canEvolve)
    {
        display.drawBitmap(tw + 1, 0, arrowUp_7x7, ARROWUP_W, ARROWUP_H, BLACK);
        tw += 1 + ARROWUP_W;
    }

    display.setCursor(tw, 0);
    display.print(" ");
    tw += 6;

    uint8_t xpPct = (uint8_t)((uint32_t)pet.xp * 100 / max(pet.xpToNext, (uint32_t)1));
    drawProgressBar(tw + 1, 1, 128 - tw - 2, 5, xpPct);
}

// ---- MAIN SCREEN ----

void drawMainScreen()
{
    drawPetHeader();
    display.drawFastHLine(0, 9, 128, BLACK);

    drawPetSprite(walkX, walkY, pet.type, animationCurrentFace(), walkFrame, walkDirX > 0);
    if (pet.sleeping)
        drawSleepAnimation();
    else
        drawEmoteBubble(walkX);
}

// ---- STATS SCREEN ----

void drawStatRow(const char* label, int16_t y, uint8_t val)
{
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(0, y);
    display.print(label);
    drawProgressBar(42, y + 1, 54, 6, val);
    display.setCursor(100, y);
    if (val < 100) display.print(" ");
    if (val < 10)  display.print(" ");
    display.print(val);
}

// Shared header for both stats pages: "Stats" left, "LvN XPcur/max" right, rule at y=9.
static void drawStatsHeader()
{
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(0, 0);
    display.print("Stats");

    int16_t lx = 128;
    // snprintf into a static buffer instead of three String temporaries (per-frame alloc).
    static char right[28];
    snprintf(right, sizeof(right), "Lv%u XP%lu/%lu", (unsigned)pet.level,
             (unsigned long)pet.xp, (unsigned long)pet.xpToNext);
    lx -= (int16_t)strlen(right) * 6;
    display.setCursor(lx, 0);
    display.print(right);

    display.drawFastHLine(0, 9, 128, BLACK);
}

void drawStatsScreen()
{
    drawStatsHeader();
    drawStatRow("Hunger",  13, pet.hunger);
    drawStatRow("Happy",   23, pet.happiness);
    drawStatRow("Energy",  33, pet.energy);
    drawStatRow("Health",  43, pet.health);
    drawStatRow("Clean",   53, pet.cleanliness);
}

// Second stats page: lifetime counters on the same rows (UP toggles from the bars).
void drawStats2Screen()
{
    drawStatsHeader();

    // row 1: name + age (right-aligned). 30 days = 1 month; below a month show days only.
    display.setCursor(0, 13);
    display.print(pet.name);
    uint16_t months = pet.ageDays / 30;
    uint16_t days   = pet.ageDays % 30;
    static char age[16];
    if (months > 0) snprintf(age, sizeof(age), "Age %umo %ud", months, days);
    else            snprintf(age, sizeof(age), "Age %ud", pet.ageDays);
    display.setCursor(128 - (int16_t)strlen(age) * 6, 13);
    display.print(age);

    /*
    display.setCursor(0, 23);
    display.print("Feed ");
    display.print(pet.feedCount);
    display.print(" Play ");
    display.print(pet.playCount);
    display.print(" Sleep ");
    display.print(pet.sleepCount);
    display.print(" Wash ");
    display.print(pet.cleanCount); 
    */

    // row 2: total XP gained over the pet's life
    display.setCursor(0, 23);
    display.print("Gained ");
    display.print(pet.totalXp);
    display.print(" XP");

    // row 3: total HP healed
    display.setCursor(0, 33);
    display.print("Restored ");
    display.print(pet.healedHp);
    display.print(" HP");

    // row 4-5: battle record (only once evolved)
    if (pet.evolved)
    {
        display.setCursor(0, 43);
        display.print("Won ");
        display.print(pet.battlesWon);
        display.print(" Battles");
        display.setCursor(0, 53);
        display.print("Lost ");
        display.print(pet.battlesLost);
        display.print(" Fights");
    }
}

// ---- MENU ICONS (Sprites) ----

static const int16_t MENU_SMALL_R = 8;
static const int16_t MENU_LARGE_R = 12;

static const uint8_t* menuSprites[8] = {
    m_feed_25x25,
    m_play_25x25,
    m_sleep_25x25,
    m_clean_25x25,
    m_heal_25x25,
    m_evolve_25x25,
    m_battle_25x25,
    m_exit_25x25,
};

static const uint8_t* menuSprSmall[8] = {
    m_feed__20x20,
    m_play__20x20,
    m_sleep__20x20,
    m_clean__20x20,
    m_heal__20x20,
    m_evolve__20x20,
    m_battle__20x20,
    m_exit__20x20,
};

static void drawMenuIcon(uint8_t idx, int16_t cx, int16_t cy, int16_t r)
{
    uint8_t a = menuActionIdx(idx);
    if (pet.sleeping) a = 2;
    if (a > 7) return;

    if (r == MENU_LARGE_R)
    {
        if (cx + MENU_W/2 < 0 || cx - MENU_W/2 >= 128) return;
        if (cy + MENU_H/2 < 0 || cy - MENU_H/2 >= 64) return;
        display.drawBitmap(cx - MENU_W/2, cy - MENU_H/2, menuSprites[a], MENU_W, MENU_H, BLACK);
    }
    else
    {
        if (cx + MENU_SMALL_W/2 < 0 || cx - MENU_SMALL_W/2 >= 128) return;
        if (cy + MENU_SMALL_H/2 < 0 || cy - MENU_SMALL_H/2 >= 64) return;
        display.drawBitmap(cx - MENU_SMALL_W/2, cy - MENU_SMALL_H/2, menuSprSmall[a], MENU_SMALL_W, MENU_SMALL_H, BLACK);
    }
}

// ---- MENU ----

static const int16_t MENU_LEFT_X = 14;
static const int16_t MENU_CENTER_X = 64;
static const int16_t MENU_RIGHT_X = 114;
static const int16_t MENU_ICON_CY = 26;

static int8_t wrapIdx(int8_t idx, int8_t delta)
{
    int8_t cnt = menuCount();
    int8_t r = idx + delta;
    while (r < 0) r += cnt;
    while (r >= cnt) r -= cnt;
    return r;
}

void drawMenuScreen()
{
    int8_t cnt = menuCount();
    if (menuIndex >= cnt) menuIndex = cnt - 1;

    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(0, 0);
    display.print("Menu");
    display.drawFastHLine(0, 10, 128, BLACK);

    if (cnt == 1)
    {
        drawMenuIcon(0, MENU_CENTER_X, MENU_ICON_CY, MENU_LARGE_R);
        display.setTextSize(2);
        int16_t nw = 5 * 12;
        display.setCursor((128 - nw) / 2, 45);
        display.print("Awake");
        return;
    }

    int8_t left  = wrapIdx(menuIndex, -1);
    int8_t right = wrapIdx(menuIndex, 1);
    drawMenuIcon(left,  MENU_LEFT_X,   MENU_ICON_CY, MENU_SMALL_R);
    drawMenuIcon(menuIndex, MENU_CENTER_X, MENU_ICON_CY, MENU_LARGE_R);
    drawMenuIcon(right, MENU_RIGHT_X,  MENU_ICON_CY, MENU_SMALL_R);

    const char* name = (menuIndex == 2 && pet.sleeping) ? "Awake" : menuLabels[menuActionIdx(menuIndex)];
    display.setTextSize(2);
    display.setTextColor(BLACK);
    display.setCursor(MENU_LEFT_X - 6, 45);
    display.print("<");
    display.setCursor(MENU_RIGHT_X - 6, 45);
    display.print(">");
    int16_t nw = (int16_t)strlen(name) * 12;
    display.setCursor((128 - nw) / 2, 45);
    display.print(name);
}

int8_t uiGetMenuIndex() { return menuIndex; }
void uiSetMenuIndex(int8_t i) { menuIndex = i; }

void uiMenuUp()  { menuIndex = wrapIdx(menuIndex, -1); }
void uiMenuDown(){ menuIndex = wrapIdx(menuIndex, 1); }

// ---- DEAD SCREEN ----

void drawDeadScreen()
{
    display.setTextSize(2);
    display.setTextColor(BLACK);
    display.setCursor(10, 20);
    display.print("DIED");

    drawPetSprite(128 - BODY_W - 7, (64 - BODY_H) / 2 - 7, pet.type, FACE_DEAD, 1, false);

    display.drawFastHLine(0, 54, 128, BLACK);
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("BURY");
    display.setCursor(128 - 6 * 6, 56);
    display.print("REVIVE");
}

// ---- TYPE SELECTION SCREEN ----

void drawSelectTypeScreen()
{
    display.setTextSize(1);
    display.setTextColor(BLACK);

    uint8_t selVariant = (millis() / 1500) % 2;
    drawPetSprite(71, 0, selectedType, animationCurrentFace(), 1, false, true, selVariant);

    static char name[16];
    snprintf(name, sizeof(name), "<%s>", typeLabels[selectedType]);
    display.setCursor(14, 24);
    display.print(name);
}

// ---- NAME ENTRY SCREEN ----

void drawEnterNameScreen()
{
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(0, 0);
    display.print("Name: ");
    display.print(nameBuf);
    if (nameLen < PET_MAX_NAME)
    {
        display.setCursor((int16_t)(36 + nameLen * 6), 0);
        display.print("_");
    }
    display.setCursor(110, 0);
    display.print(nameLen);
    display.print("/");
    display.print(PET_MAX_NAME);
    display.drawFastHLine(0, 10, 128, BLACK);

    char c = charset[charsetPos];
    display.setTextSize(3);
    int16_t x1, y1; uint16_t w, h;
    char cc[2] = { c, '\0' };                 // no String temporary in the per-frame path
    display.getTextBounds(cc, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2 + 1, 21);
    display.print(c);

    display.setTextSize(2);
    char p = charset[(charsetPos + CHARSET_LEN - 1) % CHARSET_LEN];
    display.setCursor((128 - w) / 2 - 18, 25);
    display.print(p);
    char n = charset[(charsetPos + 1) % CHARSET_LEN];
    display.setCursor((128 - w) / 2 + w + 6, 25);
    display.print(n);

    display.drawFastHLine(0, 52, 128, BLACK);

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print("<-");
    display.setCursor(55, 56);
    display.print("OK");
    display.setCursor(96, 56);
    display.print("SHIFT");
}

// ---- CONFIRM NAME SCREEN ----

void drawConfirmNameScreen()
{
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(0, 0);
    display.print("You choose name");

    const char* name = uiGetNameBuf();
    if (name[0] == '\0')
        name = petDefaultName(pet.type);
    int16_t nw = (int16_t)strlen(name) * 12;
    display.setTextSize(2);
    display.setCursor((128 - nw) / 2, 14);
    display.print(name);

    display.setTextSize(1);
    display.setCursor(90, 36);
    display.print("right?");

    display.drawFastHLine(0, 50, 128, BLACK);
    display.setCursor(0, 54);
    display.print("NO");
    display.setCursor(110, 54);
    display.print("YES");
}

// ---- HATCHING SCREEN ----

void drawHatchingScreen()
{
    display.setTextSize(1);
    display.setTextColor(BLACK);
    static char header[26];
    snprintf(header, sizeof(header), "%s Lv.%u ", pet.name.c_str(), (unsigned)pet.level);
    int16_t tw = (int16_t)strlen(header) * 6;
    display.setCursor(0, 0);
    display.print(header);
    drawProgressBar(tw + 1, 1, 128 - tw - 2, 5, pet.incubationProgress);
    display.drawFastHLine(0, 9, 128, BLACK);

    int16_t cx = (128 - INCUBATOR_W) / 2;
    int16_t cy = 64 - INCUBATOR_H;

    if (pet.stage == 2)
    {
        uint32_t t = millis();
        uint8_t bodyFrame = (t / 200) % 3;
        bool eyeHappy = ((t / 600) % 2) == 0;
        uint8_t faceIdx = eyeHappy ? FACE_HAPPY : FACE_IDLE1;
        drawPetSprite(cx + 1, cy - 6, pet.type, faceIdx, bodyFrame, false);
    }
    else
    {
        if (pet.stage == 1)
        {
            int16_t crackX = cx + (INCUBATOR_W - CRACK_W) / 2;
            int16_t crackY = cy + (INCUBATOR_H - CRACK_H) / 2;

            display.drawBitmap(crackX, crackY, inc_crack1_27x40, CRACK_W, CRACK_H, BLACK);
            if (hatchPressCount >= 1)
                display.drawBitmap(crackX, crackY, inc_crack2_27x40, CRACK_W, CRACK_H, BLACK);
            if (hatchPressCount >= 2)
                display.drawBitmap(crackX, crackY, inc_crack3_27x40, CRACK_W, CRACK_H, BLACK);
            if (millis() < readyFlashEnd)
                display.drawBitmap(crackX, crackY, inc_crack_27x40, CRACK_W, CRACK_H, BLACK);
        }

        const uint8_t* cubeSprite = (pet.incubationProgress >= 85) ? cub_inc2_20x20 : cub_inc_20x20;
        int8_t cubeOffY = (int8_t)(sinf((float)millis() / 4800.0f * 6.2832f) * 5.0f - 1.0f);
        int8_t cubeOffX = (int8_t)(sinf((float)millis() / 3100.0f * 6.2832f) * 3.0f);
        int16_t cubeX = cx + (INCUBATOR_W - CUBE_W) / 2 + cubeOffX;
        int16_t cubeY = cy + (INCUBATOR_H - CUBE_H) / 2 + cubeOffY;
        display.drawBitmap(cubeX, cubeY, cubeSprite, CUBE_W, CUBE_H, BLACK);
    }
}

// ---- CONFIRM RESET SCREEN ----

static void drawConfirmResetScreen()
{
    display.setTextSize(2);
    display.setTextColor(BLACK);
    display.setCursor(4, 4);
    display.print("RESET PET?");
    display.setTextSize(1);
    display.setCursor(12, 26);
    display.print("All data will be");
    display.setCursor(20, 36);
    display.print("lost forever!");
    display.drawFastHLine(0, 54, 128, BLACK);
    display.setCursor(0, 56);
    display.print("NO");
    display.setCursor(55, 56);
    display.print("YES");
    display.setCursor(115, 56);
    display.print("NO");
}

// ---- BACKGROUND ----

static void drawBackground(Screen s)
{
    const uint8_t* bg;
    switch (s)
    {
        case SCREEN_SELECT_TYPE:  bg = BG_SELECT_TYPE; break;
        case SCREEN_ENTER_NAME:   bg = BG_ENTER_NAME; break;
        case SCREEN_MAIN:
            if (pet.evolved)
            {
                static const uint8_t* idleBgs[] = { bg2_128x64, bg3_128x64, bg4_128x64, bg5_128x64, bg6_128x64 };
                static uint8_t idleBgIdx = 0;
                static bool prevIdle = false;
                if (uiIdle && !prevIdle)
                    idleBgIdx = random(0, sizeof(idleBgs) / sizeof(idleBgs[0]));
                prevIdle = uiIdle;
                bg = idleBgs[idleBgIdx];
            }
            else
            {
                bg = BG_MAIN_BABY;
            }
            break;
        case SCREEN_STATS:          bg = BG_STATS; break;
        case SCREEN_STATS2:         bg = BG_STATS; break;
        case SCREEN_MENU:           bg = BG_MENU; break;
        case SCREEN_INFO:           bg = BG_INFO; break;
        case SCREEN_EVOLUTION:      bg = BG_EVOLUTION; break;
        case SCREEN_DEAD:           bg = BG_DEAD; break;
        case SCREEN_HATCHING:       bg = BG_HATCHING; break;
        case SCREEN_CONFIRM_RESET:  bg = BG_CONFIRM_RESET; break;
        case SCREEN_CONFIRM_NAME:   bg = BG_CONFIRM_NAME; break;
        case SCREEN_BATTLE_SEARCH:  bg = BG_BATTLE_SEARCH; break;
        case SCREEN_BATTLE:         bg = BG_BATTLE; break;
        case SCREEN_BATTLE_WIN:     bg = BG_BATTLE_WIN; break;
        case SCREEN_BATTLE_LOSE:    bg = BG_BATTLE_LOSE; break;
        case SCREEN_BATTLE_DRAW:    bg = BG_BATTLE_DRAW; break;
        case SCREEN_LOW_HP:         bg = BG_LOW_HP; break;
        case SCREEN_WELCOME:        bg = BG_WELCOME; break;
        default:                    bg = BG_BLANK; break;
    }
    if (bg == BG_BLANK)
    {
        display.clearDisplay();
    }
    else if (bg == BG_FILL)
    {
        display.fillScreen(BLACK);
    }
    else
    {
        display.clearDisplay();
        display.drawBitmap(0, 0, bg, BG_W, BG_H, BLACK);
    }
}

static void drawLowHpScreen()
{
    display.setTextSize(2);
    display.setCursor(29, 6);
    display.print("LOW HP");
    display.setTextSize(1);
    display.setCursor(25, 30);
    display.print("HEAL FIRST");
    display.drawBitmap(90, 26, emoteSick_15x15, 15, 15, BLACK);
    display.setCursor(26, 53);
    display.print("PRESS ANY KEY");
}

static void drawWelcomeScreen()
{
    display.setTextSize(1);
    display.setTextColor(BLACK);
    display.setCursor(35, 10);
    display.print("WELCOME TO");
    display.setTextSize(2);
    display.setCursor(23, 25);
    display.print("OFFZONE");
    display.setTextSize(1);
    display.setCursor(35, 47);
    display.print("LABORATORY");
}

// ---- DISPATCH ----

void uiDraw()
{
    drawBackground(currentScreen);
    switch (currentScreen)
    {
        case SCREEN_SELECT_TYPE:  drawSelectTypeScreen(); break;
        case SCREEN_ENTER_NAME:   drawEnterNameScreen(); break;
        case SCREEN_MAIN:         drawMainScreen(); break;
        case SCREEN_STATS:        drawStatsScreen(); break;
        case SCREEN_STATS2:       drawStats2Screen(); break;
        case SCREEN_MENU:         drawMenuScreen(); break;
        case SCREEN_DEAD:         drawDeadScreen(); break;
        case SCREEN_HATCHING:     drawHatchingScreen(); break;
        case SCREEN_CONFIRM_RESET: drawConfirmResetScreen(); break;
        case SCREEN_CONFIRM_NAME:  drawConfirmNameScreen(); break;
        case SCREEN_BATTLE_SEARCH:
        case SCREEN_BATTLE:
        case SCREEN_BATTLE_WIN:
        case SCREEN_BATTLE_LOSE:
        case SCREEN_BATTLE_DRAW:   battleDraw(); break;
        case SCREEN_LOW_HP:        drawLowHpScreen(); break;
        case SCREEN_WELCOME:       drawWelcomeScreen(); break;
        default:                  drawMainScreen(); break;
    }
    display.display();
}

void uiUpdate() { uiDraw(); }

void uiInit()
{
    display.begin();
    display.setRotation(DISPLAY_ROTATE ? 2 : 0);
    display.setContrast(DISPLAY_CONTRAST);
    display.clearDisplay();
    display.display();
    delay(200);
    display.clearDisplay();
    display.display();
    Serial.println("HW  | ST7567 ready");
}
