#include "pet.h"
#include "config.h"

Pet pet;
static uint32_t battleEndTime = 0;

const char* petDefaultName(PetType type)
{
    switch (type) {
        case CUB1: return PET1_DEFAULT_NAME;
        case CUB2: return PET2_DEFAULT_NAME;
        case CUB3: return PET3_DEFAULT_NAME;
        default:   return PET1_DEFAULT_NAME;
    }
}

void petReset()
{
    pet.name = petDefaultName(pet.type);
    pet.type = CUB1;
    pet.emotion = EMOTION_IDLE;
    pet.hunger = 50;
    pet.happiness = 80;
    pet.energy = 70;
    pet.health = 100;
    pet.cleanliness = 40;
    pet.xp = 0;
    pet.xpToNext = LEVEL_START_XP;
    pet.level = 0;
    pet.ageDays = 0;
    pet.steps = 0;
    pet.sleeping = false;
    pet.sick = false;
    pet.dead = false;
    pet.evolved = false;
    pet.leveledUp = false;
    pet.setupDone = false;
    pet.fatigueCount = 0;
    pet.stage = 0;
    pet.incubationProgress = 0;
    pet.feedCount = 0;
    pet.playCount = 0;
    pet.sleepCount = 0;
    pet.cleanCount = 0;
    pet.healedHp = 0;
    pet.battlesWon = 0;
    pet.battlesLost = 0;
    pet.totalXp = 0;
    pet.lastUpdate = millis();
}

Emotion petCalculateEmotion()
{
    if (pet.dead) return EMOTION_SAD;
    if (pet.sleeping) return EMOTION_SLEEPY;
    if (pet.sick) return EMOTION_SICK;
    if (pet.hunger < 20) return EMOTION_HUNGRY;
    if (pet.energy < 20) return EMOTION_SLEEPY;
    if (pet.cleanliness < 20) return EMOTION_DIRTY;
    if (pet.happiness < 20) return EMOTION_BORED;
    if (pet.health < 25) return EMOTION_SICK;
    if (pet.happiness > 80) return EMOTION_HAPPY;
    return EMOTION_IDLE;
}

void petGainXP(uint16_t amount)
{
    if (pet.dead) return;

    if (pet.level >= MAX_LEVEL)
    {
        pet.xp = pet.xpToNext;
        return;
    }

    uint16_t add = (uint16_t)(amount * XP_MULTIPLIER);
    pet.xp += add;
    pet.totalXp += add;   // lifetime total

    uint8_t maxLevels = 50;
    while (pet.xp >= pet.xpToNext && maxLevels-- > 0)
    {
        if (pet.xpToNext == 0) break;
        pet.xp -= pet.xpToNext;
        pet.level++;

        pet.leveledUp = true;

        if (pet.level >= MAX_LEVEL)
        {
            pet.level = MAX_LEVEL;
            pet.xp = pet.xpToNext;
            break;
        }

        pet.xpToNext = (uint32_t)(pet.xpToNext * 1.1f);
        if (pet.xpToNext > 500) pet.xpToNext = 500;
    }
}

void petSetSafePeriod()
{
    battleEndTime = millis();
}

void petUpdate()
{
    if (pet.dead) return;
    if (pet.stage < 3) return;
    uint32_t now = millis();
    if (now - pet.lastUpdate < PET_UPDATE_RATE) return;
    pet.lastUpdate = now;

    if (pet.sleeping)
    {
        pet.energy = min(100, (int)pet.energy + SLEEP_RECOVERY_RATE);
        #if AUTO_AWAKE
        if (pet.energy >= 100)
        {
            pet.sleeping = false;
            pet.fatigueCount = 0;
        }
        #endif
        // Голод течёт и во сне (медленнее, SLEEP_HUNGER_MULT) и голодная смерть работает:
        // иначе вечный сон = бессмертие и заморозка всей механики ухода.
        if (pet.sleeping)
        {
            static float sleepHungerDecay = 0;
            sleepHungerDecay += PET_UPDATE_RATE / (SLEEP_HUNGER_MULT * 10000.0f);
            int sd = (int)sleepHungerDecay;
            if (sd) { sleepHungerDecay -= sd; pet.hunger = max(0, (int)pet.hunger - sd); }
            if (pet.hunger == 0) pet.health = max(0, (int)pet.health - 2);
            if (pet.health == 0) pet.dead = true;
            pet.emotion = petCalculateEmotion();
        }
        return;
    }

    // Безопасный период после боя — статы не падают
    if (now - battleEndTime < BATTLE_SAFE_PERIOD) return;

    static float hungerDecay = 0, happyDecay = 0, energyDecay = 0, cleanDecay = 0;
    hungerDecay += PET_UPDATE_RATE / (DECAY_HUNGER_MULT * 10000.0f);
    happyDecay  += PET_UPDATE_RATE / (DECAY_HAPPINESS_MULT * 10000.0f);
    energyDecay += PET_UPDATE_RATE / (DECAY_ENERGY_MULT * 10000.0f);
    cleanDecay  += PET_UPDATE_RATE / (DECAY_CLEANLINESS_MULT * 10000.0f);
    int d;
    d = (int)hungerDecay; if (d) { hungerDecay -= d; pet.hunger = max(0, (int)pet.hunger - d); }
    d = (int)happyDecay;  if (d) { happyDecay  -= d; pet.happiness = max(0, (int)pet.happiness - d); }
    d = (int)energyDecay; if (d) { energyDecay -= d; pet.energy = max(0, (int)pet.energy - d); }
    d = (int)cleanDecay;  if (d) { cleanDecay  -= d; pet.cleanliness = max(0, (int)pet.cleanliness - d); }

    if (pet.energy == 0)
    {
        pet.fatigueCount++;
        if (pet.fatigueCount >= 2)
        {
            pet.sleeping = true;
            pet.fatigueCount = 0;
            pet.emotion = petCalculateEmotion();
            return;
        }
    }
    else
    {
        pet.fatigueCount = 0;
    }

    if (pet.hunger == 0)
    {
        pet.health = max(0, (int)pet.health - 2);
    }
    static uint8_t cleanHlthTick = 0;
    if (pet.cleanliness < 5)
    {
        cleanHlthTick++;
        if (cleanHlthTick % 3 == 0)
            pet.health = max(0, (int)pet.health - 1);
    }
    else
    {
        cleanHlthTick = 0;
    }
    static uint8_t happyHlthTick = 0;
    if (pet.happiness < 1)
    {
        happyHlthTick++;
        if (happyHlthTick % 5 == 0)
            pet.health = max(0, (int)pet.health - 1);
    }
    else
    {
        happyHlthTick = 0;
    }

    if (pet.health < 20) pet.sick = true;
    if (pet.health > 50) pet.sick = false;
    if (pet.health == 0) pet.dead = true;

    pet.emotion = petCalculateEmotion();
}

void petFeed()
{
    pet.hunger = min(100, (int)pet.hunger + 30);
    pet.cleanliness = max(0, (int)pet.cleanliness - 3);
    pet.feedCount++;
    petGainXP(XP_FEED);
}

void petPlay()
{
    if (pet.energy < 10) return;
    pet.happiness = min(100, (int)pet.happiness + 25);
    pet.energy = max(0, (int)pet.energy - 10);
    pet.hunger = max(0, (int)pet.hunger - 2);
    pet.playCount++;
    petGainXP(XP_PLAY);
}

void petSleep()
{
    pet.sleeping = true;
    pet.fatigueCount = 0;
    pet.sleepCount++;
}

void petWakeUp()
{
    pet.sleeping = false;
    pet.fatigueCount = 0;
}

void petClean()
{
    pet.cleanliness = 100;
    pet.cleanCount++;
    petGainXP(XP_CLEAN);
}

void petHeal()
{
    uint8_t before = pet.health;
    pet.health = min(100, (int)pet.health + 45);
    pet.healedHp += pet.health - before;
    pet.sick = false;
    petGainXP(XP_HEAL);
}

bool petCanEvolve()
{
    return !pet.evolved && pet.level >= EVOLUTION_LEVEL;
}

void petEvolve()
{
    if (!petCanEvolve()) return;
    pet.evolved = true;
    pet.health = 100;
    pet.energy = 100;
    pet.happiness = 100;
    petGainXP(XP_EVOLVE);
}
