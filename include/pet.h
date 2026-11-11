#pragma once

#include <Arduino.h>

#define PET_TYPE_COUNT 3

enum PetType
{
    CUB1,
    CUB2,
    CUB3
};

enum Emotion
{
    EMOTION_IDLE,
    EMOTION_HAPPY,
    EMOTION_HUNGRY,
    EMOTION_SLEEPY,
    EMOTION_SICK,
    EMOTION_SAD,
    EMOTION_EXCITED,
    EMOTION_DIRTY,   // low cleanliness — wants a wash
    EMOTION_BORED    // low happiness — wants to play
};

struct Pet
{
    String name;

    PetType type;
    Emotion emotion;

    uint8_t hunger;
    uint8_t happiness;
    uint8_t energy;
    uint8_t health;
    uint8_t cleanliness;

    uint32_t xp;
    uint32_t xpToNext;

    uint8_t level;

    uint16_t ageDays;    // +1 day of life per real hour
    uint32_t steps;

    uint8_t fatigueCount;

    bool sleeping;
    bool sick;
    bool dead;
    bool evolved;
    bool leveledUp;
    bool setupDone;

    uint32_t lastUpdate;

    uint8_t stage;
    uint8_t incubationProgress;

    // lifetime counters (for the second stats screen)
    uint16_t feedCount;
    uint16_t playCount;
    uint16_t sleepCount;
    uint16_t cleanCount;
    uint16_t healedHp;
    uint16_t battlesWon;
    uint16_t battlesLost;
    uint32_t totalXp;      // lifetime XP earned (shown as "Gained N XP")
};

extern Pet pet;

// lifecycle

const char* petDefaultName(PetType type);
void petReset();
void petUpdate();
void petSetSafePeriod();

// actions

void petFeed();
void petPlay();
void petSleep();
void petWakeUp();
void petClean();
void petHeal();

void petGainXP(uint16_t amount);

bool petCanEvolve();
void petEvolve();

Emotion petCalculateEmotion();