#include "battle.h"
#include "config.h"
#include "pet.h"
#include "ui.h"
#include "sprites.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include <ST7567.h>

extern ST7567 display;

// ========== STATE ==========

// battleState machine phases (kept as raw values for the extern in battle.h)
enum {
    BATTLE_IDLE      = 0,
    BATTLE_SEARCHING = 1,
    BATTLE_GET_READY = 3,
    BATTLE_FIGHTING  = 4,
    BATTLE_RESULT    = 5,
    BATTLE_NO_ENEMY  = 6,
    BATTLE_END_HOLD  = 7,
};

uint8_t battleState = BATTLE_IDLE;

static BattlePet myPet;
static BattlePet enemyPet;
static BattleRound rounds[BATTLE_MAX_ROUNDS];
static uint8_t numRounds;
static uint8_t displayRound;
static uint8_t battleResult;
static uint16_t lastDamage;
static uint32_t roundTimer;

static bool bleInited;
static bool preinitDone;
static uint32_t searchStartTime;
static uint32_t msgStartTime;
static BLEScan* pScan;
static BLEAdvertising* pAdv;
static bool hasTarget;
static uint32_t enemyHitFlashEnd = 0;
static uint8_t ownAddr[6];
static bool scanActive;
static uint32_t foundTime;
static uint32_t stateEnterTime;
static uint16_t battleEndMyHp;

// --- matchmaking: mutual-target pairing so exactly two badges fight ---
static uint16_t myId;                     // our short id = last 2 bytes of our address
static uint16_t myTargetId;               // id of the peer we are currently targeting
static volatile uint16_t bestCandId;      // lowest-id valid candidate we can see (0 = none)
static volatile uint16_t bestCandTarget;  // that candidate's advertised target id
static BattlePet bestCandPet;             // that candidate's stats
// onResult() runs on the BLE host task while the main loop reads the candidate:
// guard the whole triple (id + target + stats) so we can never copy a torn mix.
static portMUX_TYPE candMux = portMUX_INITIALIZER_UNLOCKED;

static inline uint16_t addrId(const uint8_t* a) { return ((uint16_t)a[4] << 8) | a[5]; }

// ========== Music state ==========
static uint32_t musicPhaseStart;
static uint8_t musicNoteIdx;
static uint8_t musicPhase;
static bool musicActive;

static bool validatePetStats(BattlePet* p);
static void parsePetInfo(uint8_t* buf, BattlePet* p);

class ScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) {
        if (!(dev.haveServiceData() &&
              dev.getServiceDataUUID().equals(BLEUUID(BATTLE_SERVICE_UUID16)))) return;

        std::string data = dev.getServiceData();
        if (data.length() < BATTLE_PKT_SIZE) return;
        const uint8_t* buf = (const uint8_t*)data.data();

        uint8_t* addr = (uint8_t*)dev.getAddress().getNative();
        if (memcmp(addr, ownAddr, 6) == 0) return;      // ignore ourselves
        uint16_t candId = addrId(addr);
        if (candId == 0) return;

        BattlePet tmp;
        parsePetInfo((uint8_t*)buf, &tmp);
        if (!validatePetStats(&tmp)) return;            // ignore garbage / hostile

        uint16_t candTarget = buf[9 + NAME_LEN] | ((uint16_t)buf[10 + NAME_LEN] << 8);

        // Keep the lowest-id candidate (deterministic, so both sides converge on
        // the same pair) and refresh it every time we see it, so we notice the
        // moment it starts targeting us back.
        if (bestCandId == 0 || candId <= bestCandId) {
            portENTER_CRITICAL(&candMux);
            bestCandId     = candId;
            bestCandPet    = tmp;
            bestCandTarget = candTarget;
            portEXIT_CRITICAL(&candMux);
        }
    }
};

// Single reusable scan callback — allocating a new one per search leaked heap.
static ScanCB scanCallback;

// ========== HELPERS ==========

static bool validatePetStats(BattlePet* p)
{
    // Reject garbage/hostile advertisements: values must be sane and self-consistent.
    return p->level > 0
        && p->hp > 0 && p->hp <= p->maxHp
        && p->maxHp > 0 && p->maxHp <= 2000
        && p->attack > 0 && p->attack <= 1000;
}

static void calcBattleStats(BattlePet* p)
{
    uint16_t maxHp = 30 + p->level * 4;
    p->maxHp = maxHp;
    p->hp = maxHp * pet.health / 100;
    if (p->hp < 1) p->hp = 1;
    p->attack = 3 + p->level * 4 / 5 + (pet.energy * 6 + pet.hunger * 4 + pet.happiness * 3 + pet.cleanliness * 2) / 80;
    memset(p->name, 0, NAME_LEN);
    for (uint8_t i = 0; i < NAME_LEN - 1 && (size_t)i < pet.name.length(); i++)
        p->name[i] = pet.name[i];
    if (pet.name.length() == 0) p->name[0] = '?';
    p->name[NAME_LEN - 1] = '\0';
}

static void prepareMyPet()
{
    myPet.level = pet.level;
    calcBattleStats(&myPet);
    myPet.type = pet.type;
    myPet.evolved = pet.evolved;
}

static void buildPetInfo(uint8_t* buf, BattlePet* p)
{
    buf[0] = p->type;
    buf[1] = p->evolved ? 1 : 0;
    buf[2] = p->level;
    buf[3] = p->hp & 0xFF;
    buf[4] = (p->hp >> 8) & 0xFF;
    buf[5] = p->maxHp & 0xFF;
    buf[6] = (p->maxHp >> 8) & 0xFF;
    buf[7] = p->attack & 0xFF;
    buf[8] = (p->attack >> 8) & 0xFF;
    memcpy(buf + 9, p->name, NAME_LEN);
    // matchmaking: who we're currently trying to fight (0 = nobody yet)
    buf[9 + NAME_LEN]  = myTargetId & 0xFF;
    buf[10 + NAME_LEN] = (myTargetId >> 8) & 0xFF;
}

static void parsePetInfo(uint8_t* buf, BattlePet* p)
{
    p->type = buf[0];
    p->evolved = buf[1] != 0;
    p->level = buf[2];
    p->hp = buf[3] | ((uint16_t)buf[4] << 8);
    p->maxHp = buf[5] | ((uint16_t)buf[6] << 8);
    p->attack = buf[7] | ((uint16_t)buf[8] << 8);
    memcpy(p->name, buf + 9, NAME_LEN);
    p->name[NAME_LEN - 1] = '\0';   // guard: never trust termination of an over-the-air name
}

// ========== BATTLE SIMULATION ==========

static void simulateBattle()
{
    uint16_t hpMe = myPet.hp;
    uint16_t hpEnemy = enemyPet.hp;
    uint16_t atkMe = myPet.attack;
    uint16_t atkEnemy = enemyPet.attack;
    uint32_t totalMe = (uint32_t)hpMe + atkMe + myPet.level * 3;
    uint32_t totalEnemy = (uint32_t)hpEnemy + atkEnemy + enemyPet.level * 3;
    // Turn order must be identical on both devices. Inputs are symmetric, so on an
    // exact total tie fall back to name/maxHp/attack — otherwise both sides pick the
    // same meFirst and their independent simulations diverge.
    bool meFirst;
    if (totalMe != totalEnemy)
        meFirst = totalMe < totalEnemy;
    else
    {
        int c = strcmp(myPet.name, enemyPet.name);
        if (c != 0) meFirst = c < 0;
        else if (myPet.maxHp != enemyPet.maxHp) meFirst = myPet.maxHp < enemyPet.maxHp;
        else meFirst = myPet.attack < enemyPet.attack;
    }
    uint32_t seed = (uint32_t)(myPet.level + enemyPet.level) * 100
                  + (uint32_t)(myPet.hp + enemyPet.hp) * 10
                  + (uint32_t)(myPet.attack + enemyPet.attack)
                  + (uint32_t)(myPet.maxHp + enemyPet.maxHp);
    randomSeed(seed);
    uint8_t r = 0;
    while (hpMe > 0 && hpEnemy > 0 && r < BATTLE_MAX_ROUNDS)
    {
        bool attackerIsMe = (r % 2 == 0) ? meFirst : !meFirst;
        uint16_t dmg = attackerIsMe ? atkMe : atkEnemy;
        dmg += (uint8_t)(random(4));
        uint16_t* defHP = attackerIsMe ? &hpEnemy : &hpMe;
        *defHP = (*defHP > dmg) ? *defHP - dmg : 0;
        rounds[r].attackerId = attackerIsMe ? 0 : 1;
        rounds[r].damage = dmg;
        rounds[r].defenderHP = *defHP;
        r++;
    }
    numRounds = r;
    if (hpMe == 0 && hpEnemy == 0) battleResult = 0;
    else if (hpMe == 0) battleResult = 2;
    else if (hpEnemy == 0) battleResult = 1;
    else battleResult = 0;
    battleEndMyHp = (battleResult == 2) ? 1 : hpMe;
}

// ========== BLE INIT / ADVERTISING ==========

// Leaderboard pet-type code: 1=baby, 2=red, 3=green, 4=blue.
static uint8_t statsPetTypeCode()
{
    if (!pet.evolved) return 1;      // baby (not evolved)
    switch (pet.type) {
        case CUB3: return 2;         // red   (Rock)
        case CUB1: return 3;         // green (Nature)
        case CUB2: return 4;         // blue  (Techno)
        default:   return 1;
    }
}

// Fill the 17-byte leaderboard packet (layout documented in battle.h).
static void buildStatsPacket(uint8_t* buf)
{
    uint8_t idHi = ownAddr[4];
    uint8_t idLo = ownAddr[5];
    buf[0] = idHi;
    buf[1] = idLo;
    buf[2] = statsPetTypeCode();
    buf[3] = pet.level;
    buf[4] = pet.battlesWon & 0xFF;
    buf[5] = (pet.battlesWon >> 8) & 0xFF;
    buf[6] = pet.battlesLost & 0xFF;
    buf[7] = (pet.battlesLost >> 8) & 0xFF;
    memset(buf + 8, 0, STATS_NAME_LEN);
    for (uint8_t i = 0; i < STATS_NAME_LEN && (size_t)i < pet.name.length(); i++)
        buf[8 + i] = pet.name[i];
    uint8_t sum = idHi + idLo + pet.level + buf[2] + buf[6] + buf[7];
    buf[16] = sum + 1;   // checksum: sum of (id+level+type+losses) then +1
}

// ---- bursty stats broadcast (battery- and heat-friendly) ----
// Instead of advertising non-stop, the badge sends a short burst of a few
// packets once every STATS_BURST_PERIOD_MS and then keeps the radio silent.
// Radio duty cycle drops from 100% to ~2%, which saves battery and heat, and
// stops flooding the receiver.
#define STATS_BURST_PERIOD_MS  10000   // one burst every 10 s
#define STATS_BURST_LEN_MS     220     // burst window (~3-4 adverts at 50ms), then off
#define STATS_ADV_MIN_UNITS    0x30    // 0x30 * 0.625ms = 30 ms
#define STATS_ADV_MAX_UNITS    0x50    // 0x50 * 0.625ms = 50 ms

static uint32_t statsBurstStart = 0;
static bool     statsBursting   = false;

// Advertise our leaderboard stats — starts one burst (fresh packet + radio on).
void battleAdvertiseStats()
{
    if (!pAdv || !bleInited) return;
    uint8_t data[STATS_PKT_SIZE];
    buildStatsPacket(data);

    BLEAdvertisementData advData;
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC);
    advData.setServiceData(BLEUUID(STATS_SERVICE_UUID16),
        std::string((char*)data, STATS_PKT_SIZE));

    pAdv->stop();
    pAdv->setAdvertisementData(advData);
    pAdv->setMinInterval(STATS_ADV_MIN_UNITS);   // fast interval so a burst fits a few packets
    pAdv->setMaxInterval(STATS_ADV_MAX_UNITS);
    pAdv->start();
}

// Drive the burst state machine — call this every main-loop iteration.
// Only broadcasts while idle; during a battle the radio is owned by the fight.
void battleStatsTick()
{
    if (!pAdv || !bleInited) return;
    if (battleState != BATTLE_IDLE) return;

    uint32_t now = millis();
    if (!statsBursting) {
        if (now - statsBurstStart >= STATS_BURST_PERIOD_MS) {
            statsBurstStart = now;
            statsBursting = true;
            battleAdvertiseStats();     // build fresh packet + start advertising
        }
    } else if (now - statsBurstStart >= STATS_BURST_LEN_MS) {
        statsBursting = false;
        pAdv->stop();                   // radio off until the next burst
    }
}

// How long (ms) it's safe to light-sleep before the next stats burst is due.
// Returns 0 when a burst is in progress or due right now (keep looping fast then).
uint32_t battleStatsSleepBudgetMs()
{
    if (statsBursting) return 0;                 // radio on, don't sleep through it
    if (battleState != BATTLE_IDLE) return 0;    // in a battle: the loop must run
    uint32_t elapsed = millis() - statsBurstStart;
    if (elapsed >= STATS_BURST_PERIOD_MS) return 0;
    return STATS_BURST_PERIOD_MS - elapsed;
}

bool battleIsIdle() { return battleState == BATTLE_IDLE; }

// Return to idle advertising: broadcast our leaderboard stats (not battle data),
// so we are never seen as a battle target while idle.
static void resetAdvertising()
{
    // leaving battle: stop the battle advert and let the stats burst restart now
    if (pAdv && bleInited) pAdv->stop();
    statsBursting   = false;
    statsBurstStart = 0;   // 0 => the next battleStatsTick fires a burst immediately
}

void battleAdvUpdate()
{
    if (!pAdv || !bleInited) return;
    prepareMyPet();
    uint8_t myData[BATTLE_PKT_SIZE];
    buildPetInfo(myData, &myPet);

    BLEAdvertisementData advData;
    advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC);
    advData.setServiceData(BLEUUID(BATTLE_SERVICE_UUID16),
        std::string((char*)myData, BATTLE_PKT_SIZE));

    pAdv->stop();
    pAdv->setAdvertisementData(advData);
    pAdv->start();
}

void battlePreinit()
{
    if (preinitDone) return;
    preinitDone = true;

    BLEDevice::init("OFFGOTCHI");
    delay(100);

    BLEAddress own = BLEDevice::getAddress();
    memcpy(ownAddr, (uint8_t*)own.getNative(), 6);
    myId = addrId(ownAddr);
    Serial.print("BLE | Address: ");
    for (int i = 0; i < 6; i++) { Serial.print(ownAddr[i], HEX); Serial.print(" "); }
    Serial.println();

    pAdv = BLEDevice::getAdvertising();
    bleInited = true;
    statsBurstStart = millis();
    statsBursting   = true;
    battleAdvertiseStats();   // immediate first stats burst (battleStatsTick ends it)

    Serial.println("BLE | Battle Preinit Done (Stats Broadcast)");
}

// ========== BATTLE MUSIC ==========

#define MUSIC_RELEASE_MS 20
#define MELODY_LEN 128

static const uint16_t melodyFreq[MELODY_LEN] = {
    // Section 1 (0-1.5s): 8 × 187ms — explosive heroic intro
    247, 185, 147, 124, 196, 165, 139, 110,
    // Section 2 (1.5-3s): 8 × 187ms — climbing energy
    124, 147, 185, 247, 277, 220, 185, 147,
    // Section 3 (3-4s): 8 × 125ms — tension buildup
    165, 196, 247, 330, 370, 0, 185, 0,
    // Section 4 (4-5s): 8 × 125ms — climactic finish
    392, 370, 330, 294, 277, 247, 233, 185,
    // ========== POST-32: varied development ==========
    // Flourish — octave alternation
    247, 392, 247, 392, 330, 494, 330, 494,
    // Descending with bounce
    440, 330, 247, 165, 392, 330, 247, 185,
    // Rhythmic drive
    185, 247, 330, 247, 185, 247, 370, 247,
    // Stepwise climb
    165, 185, 196, 220, 247, 277, 294, 330,
    // Higher fanfare
    330, 494, 392, 494, 370, 494, 392, 494,
    // Descending sequence
    494, 440, 392, 330, 440, 392, 330, 247,
    // Alternating thirds
    185, 247, 185, 247, 165, 220, 165, 220,
    // Building back up
    247, 330, 392, 494, 330, 392, 494, 440,
    // Peak energy
    440, 494, 440, 392, 330, 247, 330, 392,
    // Descending transition
    392, 330, 247, 185, 330, 247, 185, 165,
    // Approaching loop
    165, 220, 247, 330, 185, 247, 185, 247,
    // Drive into restart
    247, 185, 247, 185, 247, 330, 247, 185,
};

static const uint16_t melodyDur[MELODY_LEN] = {
    187, 187, 187, 187, 187, 187, 187, 187,
    187, 187, 187, 187, 187, 187, 187, 187,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
    125, 125, 125, 125, 125, 125, 125, 125,
};

// A tune = parallel freq/dur arrays of `len` notes; freq 0 means a rest.
struct Melody {
    const uint16_t* freq;
    const uint16_t* dur;
    uint8_t len;
};

// --- Search phase: tense, wandering A-minor loop ("scanning for a rival").
//     Ends on E (the dominant) so it flows straight back into the opening A —
//     no long tail note, so the loop has no audible seam. ---
static const uint16_t searchFreq[24] = {
    220, 262, 330, 262, 247, 220, 262, 247,
    294, 349, 330, 294, 262, 247, 220, 196,
    220, 262, 330, 440, 392, 330, 247, 330
};
static const uint16_t searchDur[24] = {
    175, 175, 175, 175, 175, 175, 175, 175,
    175, 175, 175, 175, 175, 175, 175, 175,
    175, 175, 175, 175, 175, 175, 175, 175
};
static const Melody searchMelody = { searchFreq, searchDur, 24 };

// --- Battle phase: the long heroic score (melodyFreq / melodyDur) ---
static const Melody battleMelody = { melodyFreq, melodyDur, MELODY_LEN };

static const Melody* curMelody = &searchMelody;

static void battleMusicPlay(const Melody* m)
{
    curMelody = m;
    musicNoteIdx = 0;
    musicPhase = 0;
    musicPhaseStart = 0;
    musicActive = true;
    ledcWrite(0, 0);   // silence current tone before the new melody's first note
}

static void battleMusicStop()
{
    musicActive = false;
    ledcWrite(0, 0);
}

static void advanceNote(uint32_t now)
{
    musicNoteIdx++;
    if (musicNoteIdx >= curMelody->len) musicNoteIdx = 0;
    musicPhase = 0;
    musicPhaseStart = now;
    uint16_t f = curMelody->freq[musicNoteIdx];
    if (f > 0) ledcWriteTone(0, f);
}

static void battleMusicUpdate()
{
    if (!musicActive || !curMelody) return;
    uint32_t now = millis();

    if (musicPhaseStart == 0)
    {
        musicPhaseStart = now;
        uint16_t f = curMelody->freq[0];
        if (f > 0) ledcWriteTone(0, f);
        return;
    }

    uint16_t f = curMelody->freq[musicNoteIdx];
    uint16_t d = curMelody->dur[musicNoteIdx];
    uint32_t elapsed = now - musicPhaseStart;

    if (f == 0)
    {
        if (elapsed >= d) advanceNote(now);
    }
    else if (musicPhase == 0)
    {
        if (elapsed >= d - MUSIC_RELEASE_MS)
        {
            ledcWrite(0, 0);
            musicPhase = 1;
            musicPhaseStart = now;
        }
    }
    else
    {
        if (elapsed >= MUSIC_RELEASE_MS) advanceNote(now);
    }
}

// ========== PUBLIC API ==========

void battleStartSearch()
{
    battleState = BATTLE_IDLE;
    battleResult = 0;
    numRounds = 0;
    displayRound = 0;
    lastDamage = 0;
    roundTimer = 0;
    hasTarget = false;
    foundTime = 0;
    stateEnterTime = 0;
    scanActive = false;
    myTargetId = 0;                 // not targeting anyone yet
    portENTER_CRITICAL(&candMux);   // scan callback may be mid-write: reset under the same lock
    bestCandId = 0;
    bestCandTarget = 0;
    portEXIT_CRITICAL(&candMux);
    memset(&myPet, 0, sizeof(myPet));
    memset(&enemyPet, 0, sizeof(enemyPet));
    memset(&bestCandPet, 0, sizeof(bestCandPet));
    battleEndMyHp = 0;
    battleMusicPlay(&searchMelody);

    // Update advertising data with current pet stats
    battleAdvUpdate();

    // Start scan with callback
    if (pScan) pScan->stop();
    pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(&scanCallback, true);
    // Passive scan: all matchmaking data is in the ADV packet (service data), so
    // we never need SCAN_REQ -> the radio only listens, no TX, during the search.
    pScan->setActiveScan(false);
    pScan->setInterval(160);
    pScan->setWindow(120);
    pScan->clearResults();
    pScan->start(0, nullptr, false);
    scanActive = true;

    uiSetScreen(SCREEN_BATTLE_SEARCH);
    searchStartTime = millis();
    battleState = BATTLE_SEARCHING;
}

void battleStop()
{
    if (pScan) { pScan->stop(); pScan = nullptr; }

    // Reset advertising to preinit state (no battle service data)
    resetAdvertising();

    battleState = BATTLE_IDLE;
    enemyHitFlashEnd = 0;
    scanActive = false;
    battleMusicStop();
    petSetSafePeriod();
}

bool battleDidWin() { return battleResult == 1; }
bool battleHasTarget() { return hasTarget; }
bool battleEnemyHitFlash() { return millis() < enemyHitFlashEnd; }
uint8_t battleGetRound() { return displayRound; }
uint16_t battleGetLastDamage() { return lastDamage; }

void battlePersistResult()
{
    pet.health = (uint16_t)battleEndMyHp * 100 / myPet.maxHp;
    if (pet.health < 1) pet.health = 1;
    if (battleResult == 1)      pet.battlesWon++;
    else if (battleResult == 2) pet.battlesLost++;
    // draw (0): neither counter — a timeout draw is not a loss
}

uint16_t battleGetRewardXP()
{
    if (battleDidWin())
        return XP_BATTLE_WIN + enemyPet.level * 3;
    if (battleResult == 0)
        return XP_BATTLE_DRAW + enemyPet.level * 2;
    return XP_BATTLE_LOSE + enemyPet.level * 2;
}

// ========== UPDATE ==========

void battleUpdate()
{
    uint32_t now = millis();

    if (battleState == BATTLE_IDLE) return;

    battleMusicUpdate();

    // Safety watchdog
    if (stateEnterTime == 0) stateEnterTime = now;
    if (battleState != BATTLE_IDLE && battleState != BATTLE_RESULT && now - stateEnterTime > 60000)
    {
        battleStop();
        uiSetScreen(SCREEN_MAIN);
        stateEnterTime = 0;
        return;
    }

    // ---- SEARCHING (5s, or immediate on mutual match) ----
    if (battleState == BATTLE_SEARCHING)
    {
        // Pick the lowest-id peer we can see as our target and advertise it, then
        // only lock in once that peer is targeting us back. Mutual targeting means
        // exactly two badges pair up; a third can never join a formed pair because
        // both sides are already pointing at each other.
        if (!hasTarget && bestCandId != 0)
        {
            if (myTargetId != bestCandId)
            {
                myTargetId = bestCandId;
                battleAdvUpdate();          // re-advertise with the new target id
            }
            if (bestCandTarget == myId)     // they target us back -> mutual pair
            {
                portENTER_CRITICAL(&candMux);   // copy under the lock: no torn read from the BLE task
                enemyPet = bestCandPet;
                portEXIT_CRITICAL(&candMux);
                hasTarget = true;
            }
        }

        if (hasTarget)
        {
            if (foundTime == 0)
            {
                foundTime = now;
                if (pScan) pScan->stop();
                scanActive = false;
            }
            if (now - foundTime >= 1000)
            {
                if (validatePetStats(&enemyPet))
                {
                    simulateBattle();
                    // Rival found: swap the search loop for the battle theme.
                    battleMusicPlay(&battleMelody);
                    battleState = BATTLE_GET_READY;
                    msgStartTime = now;
                }
                else
                {
                    hasTarget = false;
                    foundTime = 0;
                }
            }
        }
        else if (now - searchStartTime >= 5000)
        {
            if (pScan) pScan->stop();
            scanActive = false;
            // Stop advertising battle data so other devices don't see us as a target
            resetAdvertising();
            hasTarget = false;
            memset(&enemyPet, 0, sizeof(enemyPet));
            battleState = BATTLE_NO_ENEMY;
            msgStartTime = now;
        }
        return;
    }

    // ---- "Get Ready" (1s) ----
    if (battleState == BATTLE_GET_READY)
    {
        if (now - msgStartTime >= 1000)
        {
            roundTimer = now;
            displayRound = 0;
            battleState = BATTLE_FIGHTING;
            uiSetScreen(SCREEN_BATTLE);
        }
        return;
    }

    // ---- FIGHTING ----
    if (battleState == BATTLE_FIGHTING)
    {
        if (now - roundTimer >= 1500)
        {
            roundTimer = now;
            if (displayRound < numRounds)
            {
                lastDamage = rounds[displayRound].damage;
                displayRound++;

                    if (rounds[displayRound - 1].attackerId == 1)
                        enemyHitFlashEnd = now + 200;

                    if (displayRound == numRounds || rounds[displayRound - 1].defenderHP == 0)
                {
                    battleState = BATTLE_END_HOLD;
                    roundTimer = now;
                }
            }
        }
        return;
    }

    // ---- END-OF-BATTLE HOLD (2s, show final frame) ----
    if (battleState == BATTLE_END_HOLD)
    {
        if (now - roundTimer >= 2000)
        {
            battleMusicStop();
            resetAdvertising();
            if (battleResult == 1)
            {
                ledcWriteTone(0, 523); delay(80); ledcWrite(0, 0);
                delay(20);
                ledcWriteTone(0, 659); delay(80); ledcWrite(0, 0);
                delay(20);
                ledcWriteTone(0, 784); delay(80); ledcWrite(0, 0);
                delay(20);
                ledcWriteTone(0, 1047); delay(160); ledcWrite(0, 0);
            }
            else if (battleResult == 0)
            {
                // draw: neutral two-note, neither fanfare nor dirge
                ledcWriteTone(0, 587); delay(120); ledcWrite(0, 0);
                delay(30);
                ledcWriteTone(0, 587); delay(220); ledcWrite(0, 0);
            }
            else
            {
                ledcWriteTone(0, 440); delay(100); ledcWrite(0, 0);
                delay(30);
                ledcWriteTone(0, 349); delay(100); ledcWrite(0, 0);
                delay(30);
                ledcWriteTone(0, 294); delay(200); ledcWrite(0, 0);
            }
            battleState = BATTLE_RESULT;
            uiSetScreen(battleResult == 1 ? SCREEN_BATTLE_WIN
                       : battleResult == 2 ? SCREEN_BATTLE_LOSE
                       : SCREEN_BATTLE_DRAW);
        }
        return;
    }

    // ---- NO ENEMY (2s then menu) ----
    if (battleState == BATTLE_NO_ENEMY)
    {
        if (now - msgStartTime >= 2000)
        {
            battleStop();
            uiSetScreen(SCREEN_MAIN);
        }
        return;
    }
}

// ========== DRAWING ==========

static const uint8_t* getAttackSprite(uint8_t type)
{
    switch (type) {
        case 0: return CUB_1_attack_50x50;
        case 1: return CUB_2_attack_50x50;
        case 2: return CUB_3_attack_50x50;
        default: return CUB_1_attack_50x50;
    }
}

static const uint8_t* getAttackMask(uint8_t type)
{
    switch (type) {
        case 0: return CUB_1_att_mask_50x50;
        case 1: return CUB_2_att_mask_50x50;
        case 2: return CUB_3_att_mask_50x50;
        default: return CUB_1_att_mask_50x50;
    }
}

static void rotateBitmap(const uint8_t* src, uint8_t* dst)
{
    uint8_t bw = (BODY_W + 7) / 8;
    memset(dst, 0, BODY_SIZE);
    for (uint8_t sy = 0; sy < BODY_H; sy++)
    {
        uint8_t dy = BODY_H - 1 - sy;
        for (uint8_t sx = 0; sx < BODY_W; sx++)
        {
            uint8_t sb = sx / 8;
            uint8_t sm = 0x80 >> (sx & 7);
            if (src[sy * bw + sb] & sm)
            {
                uint8_t dx = BODY_W - 1 - sx;
                uint8_t db = dx / 8;
                uint8_t dm = 0x80 >> (dx & 7);
                dst[dy * bw + db] |= dm;
            }
        }
    }
}

static void drawAttackOverlay(int16_t x, int16_t y, uint8_t type, bool rotated)
{
    const uint8_t* sprite = getAttackSprite(type);
    const uint8_t* mask = getAttackMask(type);

    if (!rotated) {
        display.drawBitmap(x, y, mask, BODY_W, BODY_H, WHITE);
        display.drawBitmap(x, y, sprite, BODY_W, BODY_H, BLACK);
        return;
    }

    uint8_t tmpMask[BODY_SIZE];
    uint8_t tmpSprite[BODY_SIZE];
    rotateBitmap(mask, tmpMask);
    rotateBitmap(sprite, tmpSprite);
    display.drawBitmap(x, y, tmpMask, BODY_W, BODY_H, WHITE);
    display.drawBitmap(x, y, tmpSprite, BODY_W, BODY_H, BLACK);
}

void battleDraw()
{
    if (battleState == BATTLE_IDLE) return;

    display.clearDisplay();

    if (battleState == BATTLE_SEARCHING)
    {
        display.setTextSize(2);
        display.setCursor(23, 4);
        display.print("SEEKING");
        display.setCursor(35, 23);
        display.print("RIVAL");

        if (!hasTarget)
        {
            uint8_t phase = (millis() / 1000) % 3;
            const uint8_t* seekFrames[] = { seek1_15x15, seek2_15x15, seek3_15x15 };
            display.drawBitmap(36, 44, seekFrames[phase], 15, 15, BLACK);
            display.drawBitmap(57, 44, seekFrames[(phase + 1) % 3], 15, 15, BLACK);
            display.drawBitmap(78, 44, seekFrames[(phase + 2) % 3], 15, 15, BLACK);
        }
        else
        {
            display.setTextSize(2);
            display.setCursor(34, 44);
            display.print("FOUND");
        }
        return;
    }

    if (battleState == BATTLE_GET_READY)
    {
        display.setCursor(11, 5);
        display.setTextSize(2);
        display.print("GET READY");
        display.setCursor(29, 28);
        display.setTextSize(1);
        display.print("FOR THE NEXT");
        display.setCursor(29, 44);
        display.setTextSize(2);
        display.print("BATTLE");
        return;
    }

    if (battleState == BATTLE_FIGHTING || battleState == BATTLE_END_HOLD)
    {
        int16_t petY_me = 14;
        int16_t petX_me = 0;
        int16_t petY_enemy = -1;
        int16_t petX_enemy = 128 - 1 - BODY_W;

        // Attack face persists until attacked pet attacks back
        bool myHit = false, enemyHit = false;
        for (uint8_t i = 0; i < displayRound && i < numRounds; i++) {
            if (rounds[i].attackerId == 0) { enemyHit = true; myHit = false; }
            else { myHit = true; enemyHit = false; }
        }
        uint8_t myFace = myHit ? FACE_ATTACK : FACE_IDLE1;
        uint8_t enemyFace = enemyHit ? FACE_ATTACK : FACE_IDLE1;

        // Bounce + attack flash at start of round
        bool attackActive = false;
        if (displayRound > 0 && displayRound <= numRounds) {
            uint32_t t = millis() - roundTimer;
            if (t < 600) {
                attackActive = true;
                if (rounds[displayRound - 1].attackerId == 0)
                    petY_me -= 3;
                else
                    petY_enemy -= 2;
            }
        }

        drawPetSprite(petX_me, petY_me, myPet.type, myFace, 0, true);
        drawPetSprite(petX_enemy, petY_enemy, enemyPet.type, enemyFace, 0, false, enemyPet.evolved);

        // Attack overlay on defender (only during bounce)
        if (attackActive) {
            uint8_t atkType = (rounds[displayRound - 1].attackerId == 0) ? myPet.type : enemyPet.type;
            int16_t defX = (rounds[displayRound - 1].attackerId == 0) ? petX_enemy : petX_me;
            int16_t defY = (rounds[displayRound - 1].attackerId == 0) ? petY_enemy : petY_me;
            drawAttackOverlay(defX, defY, atkType, rounds[displayRound - 1].attackerId == 0);
        }

        uint16_t myHP = myPet.hp;
        uint16_t enemyHP = enemyPet.hp;
        for (uint8_t i = 0; i < numRounds && i < displayRound; i++)
        {
            if (rounds[i].attackerId == 0) enemyHP = rounds[i].defenderHP;
            else myHP = rounds[i].defenderHP;
        }

        // My stats (top-left)
        display.setTextSize(1);
        display.setCursor(0, 8);
        display.print("HP:");
        drawProgressBar(18, 9, 40, 5, map(myHP, 0, myPet.maxHp, 0, 100));

        char lvHpBuf[16];
        snprintf(lvHpBuf, sizeof(lvHpBuf), "Lv%d %d/%d", myPet.level, myHP, myPet.maxHp);
        display.setCursor(0, 0);
        display.print(lvHpBuf);

        // Enemy stats (bottom-right)
        int16_t nameLen = strlen(enemyPet.name);
        display.setCursor(128 - nameLen * 6, 49);
        display.print(enemyPet.name);

        char hpLabel[4];
        snprintf(hpLabel, sizeof(hpLabel), "HP:");
        int16_t hpLabelW = strlen(hpLabel) * 6;
        int16_t barW = 40;
        int16_t totalW = hpLabelW + barW;
        int16_t startX = 128 - totalW;

        display.setCursor(startX, 57);
        display.print(hpLabel);
        drawProgressBar(startX + hpLabelW, 58, barW, 5, map(enemyHP, 0, enemyPet.maxHp, 0, 100));

        // Damage numbers
        if (displayRound > 0 && displayRound <= numRounds)
        {
            uint8_t i = displayRound - 1;
            char dmgBuf[8];
            snprintf(dmgBuf, sizeof(dmgBuf), "-%d", rounds[i].damage);
            display.setTextSize(1);
            if (rounds[i].attackerId == 0)
                display.setCursor(petX_enemy - 16, petY_enemy + 20);
            else
                display.setCursor(petX_me + BODY_W + 0, petY_me + 20);
            display.print(dmgBuf);
        }

        return;
    }

    if (battleState == BATTLE_RESULT)
    {
        // Single source of truth for the reward — matches what petGainXP() will grant.
        uint16_t gainedXP = (uint16_t)(battleGetRewardXP() * XP_MULTIPLIER);
        display.setTextSize(2);
        if (battleResult == 1)
        {
            display.setCursor(19, 12);
            display.print("VICTORY!");
        }
        else if (battleResult == 2)
        {
            display.setCursor(17, 12);
            display.print("YOU LOSE");
        }
        else
        {
            display.setCursor(40, 12);
            display.print("DRAW");
        }
        display.setTextSize(1);
        display.setCursor(42, 33);
        display.print("+");
        display.print(gainedXP);
        display.print(" XP");
        display.setCursor(26, 53);
        display.print("PRESS ANY KEY");
        return;
    }

    if (battleState == BATTLE_NO_ENEMY)
    {
        display.setCursor(31, 17);
        display.setTextSize(1);
        display.print("THERE IS NO");
        display.setTextSize(2);
        display.setCursor(35, 31);
        display.print("ENEMY");
        return;
    }
}
