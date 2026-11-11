#pragma once

#include <Arduino.h>
#include "config.h"

#define BATTLE_SERVICE_UUID16 ((uint16_t)0xFFE0)
#define NAME_LEN (PET_MAX_NAME + 1) // доп символ для терминатора строки
// Battle advert layout: [0..8] stats, [9 .. 9+NAME_LEN-1] name, then 2 bytes
// targetId = last 2 bytes of the address of the peer this badge is trying to
// fight (0 = nobody yet). Two badges only lock into a fight when they target
// each other, so exactly two pair up and any third is left out.
#define BATTLE_PKT_SIZE (9 + NAME_LEN + 2)
#define BATTLE_MAX_ROUNDS 50

// ---- Leaderboard broadcast (advertised whenever NOT in a battle) ----
// Separate service UUID so battle scanners (0xFFE0) ignore it and the
// stat-collector receiver (0xFFE1) picks it up.
// Packet (17 bytes):
//   [0..1] device id  (last 2 bytes of the BLE address, shown as 4 hex)
//   [2]    pet type    (1=baby, 2=red, 3=green, 4=blue)
//   [3]    level
//   [4..5] battles won  (uint16 LE)
//   [6..7] battles lost (uint16 LE)
//   [8..15] name        (8 bytes, null-padded)
//   [16]   checksum = (idHi+idLo + level + type + lostLo+lostHi + 1) & 0xFF
#define STATS_SERVICE_UUID16  ((uint16_t)0xFFE1)
#define STATS_NAME_LEN        8
#define STATS_PKT_SIZE        17

struct BattlePet
{
    uint8_t type;
    bool evolved;
    uint8_t level;
    uint16_t hp;
    uint16_t maxHp;
    uint16_t attack;
    char name[NAME_LEN];
};

struct BattleRound
{
    uint8_t attackerId;
    uint16_t damage;
    uint16_t defenderHP;
};

extern uint8_t battleState;

void battlePreinit();
void battleAdvUpdate();
void battleAdvertiseStats();   // broadcast leaderboard stats (start one burst)
void battleStatsTick();        // call every loop: bursts stats every 10s, else radio off
uint32_t battleStatsSleepBudgetMs();  // ms safe to light-sleep before the next burst (0 = burst active/due)
bool battleIsIdle();           // true when not in a battle flow
void battleStartSearch();
void battleStop();
void battleUpdate();
void battleDraw();
uint8_t battleGetRound();
uint16_t battleGetLastDamage();
bool battleDidWin();
uint16_t battleGetRewardXP();
void battlePersistResult();
bool battleEnemyHitFlash();
