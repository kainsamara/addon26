#pragma once

#include <Arduino.h>

enum Screen
{
    SCREEN_SELECT_TYPE,
    SCREEN_ENTER_NAME,
    SCREEN_MAIN,
    SCREEN_STATS,
    SCREEN_STATS2,
    SCREEN_MENU,
    SCREEN_INFO,
    SCREEN_EVOLUTION,
    SCREEN_DEAD,
    SCREEN_HATCHING,
    SCREEN_CONFIRM_RESET,
    SCREEN_CONFIRM_NAME,
    SCREEN_BATTLE_SEARCH,
    SCREEN_BATTLE,
    SCREEN_BATTLE_WIN,
    SCREEN_BATTLE_LOSE,
    SCREEN_BATTLE_DRAW,
    SCREEN_LOW_HP,
    SCREEN_WELCOME
};

void uiInit();
void uiUpdate();
void uiSetScreen(Screen screen);
Screen uiGetScreen();
void uiDraw();

void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t val);

int8_t uiGetMenuIndex();
void uiSetMenuIndex(int8_t i);
void uiMenuUp();
void uiMenuDown();
int8_t menuActionIdx(int8_t menuIdx);

uint8_t uiGetSelectedType();
void uiSetSelectedType(uint8_t t);
void uiCycleType(int8_t dir);

const char* uiGetNameBuf();
void uiNameAddChar(char c);
void uiNameAddCharShift(char c);   // SHIFT: add letter with inverted auto-casing
void uiNameDeleteChar();
void uiNameReset();
uint8_t uiNameLen();

void uiCharsetUp();
void uiCharsetDown();
char uiCharsetCurrent();

#define EMOTE_NONE 255
#define EMOTE_UP_ARROW    7
#define EMOTE_GAME        8
#define EMOTE_CLEAN       9
#define EMOTE_DEAD        10
#define EMOTE_FED         11 
#define EMOTE_NEED_FOOD   12
#define EMOTE_NEED_CLEAN  13 
#define EMOTE_NEED_PLAY   14 
#define EMOTE_NEED_ENERGY 15 
#define EMOTE_HEALED      16 

extern uint32_t lastScreenChange;

bool uiInputLocked();
void uiUpdateWalking(uint32_t now);
void uiSetEmote(uint8_t emote);
uint8_t uiGetCurrentEmote();
void uiSetHatchPressCount(uint8_t count);
uint8_t uiGetHatchPressCount();
void uiSetReadyFlash();
extern bool uiIdle;

void drawPetSprite(int16_t x, int16_t y, uint8_t type, uint8_t faceIdx,
                   uint8_t frame, bool mirrored,
                   bool forceAdult = false, uint8_t forceVariant = 255);
