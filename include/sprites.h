#pragma once

#include <Arduino.h>

#define BODY_W 50
#define BODY_H 50
#define BODY_SIZE ((BODY_W + 7) / 8 * BODY_H)
#define EYE_W 15
#define EYE_H 10
#define EYE_SIZE ((EYE_W + 7) / 8 * EYE_H)
#define EYE_OX 11
#define EYE_OY 26
#define WALK_FRAMES 3

#define FACE_IDLE1  0
#define FACE_IDLE2  1
#define FACE_HAPPY  2
#define FACE_SAD    3
#define FACE_SLEEP  4
#define FACE_SICK   5
#define FACE_DEAD   6
#define FACE_ATTACK 7

const uint8_t* getBodySprite(uint8_t type, uint8_t variant, bool forceAdult = false);
const uint8_t* getMirroredBody(uint8_t type, uint8_t variant, bool forceAdult = false);
const uint8_t* getMirroredEye(uint8_t faceIdx, uint8_t type, bool forceAdult = false);
const uint8_t* getEyeSprite(uint8_t faceIdx, uint8_t type, bool forceAdult = false);

// Спрайты тушек кубов (50x50)
extern const uint8_t CUB_01_50x50[BODY_SIZE];
extern const uint8_t CUB_02_50x50[BODY_SIZE];
extern const uint8_t CUB_11_50x50[BODY_SIZE];
extern const uint8_t CUB_12_50x50[BODY_SIZE];
extern const uint8_t CUB_21_50x50[BODY_SIZE];
extern const uint8_t CUB_22_50x50[BODY_SIZE];
extern const uint8_t CUB_31_50x50[BODY_SIZE];
extern const uint8_t CUB_32_50x50[BODY_SIZE];

// Маска тушки (заливка белым перед отрисовкой тела)
extern const uint8_t CUB_01_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_02_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_11_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_12_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_21_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_22_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_31_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_32_mask_50x50[BODY_SIZE];

const uint8_t* getBodyMask(uint8_t type, uint8_t variant, bool forceAdult = false);
const uint8_t* getMirroredMask(uint8_t type, uint8_t variant, bool forceAdult = false);

// Спрайты глаз (15x10)
extern const uint8_t CUB_0_eyeIdle1_15x10[EYE_SIZE];
extern const uint8_t CUB_0_eyeIdle2_15x10[EYE_SIZE];
extern const uint8_t CUB_0_eyeHappy_15x10[EYE_SIZE];
extern const uint8_t CUB_0_eyeSleep_15x10[EYE_SIZE];
extern const uint8_t CUB_0_eyeSick_15x10[EYE_SIZE];
extern const uint8_t CUB_0_eyeDead_15x10[EYE_SIZE];

extern const uint8_t CUB_1_eyeIdle1_15x10[EYE_SIZE];
extern const uint8_t CUB_1_eyeIdle2_15x10[EYE_SIZE];
extern const uint8_t CUB_1_eyeHappy_15x10[EYE_SIZE];
extern const uint8_t CUB_1_eyeSleep_15x10[EYE_SIZE];
extern const uint8_t CUB_1_eyeSick_15x10[EYE_SIZE];
extern const uint8_t CUB_1_eyeDead_15x10[EYE_SIZE];

extern const uint8_t CUB_2_eyeIdle1_15x10[EYE_SIZE];
extern const uint8_t CUB_2_eyeIdle2_15x10[EYE_SIZE];
extern const uint8_t CUB_2_eyeHappy_15x10[EYE_SIZE];
extern const uint8_t CUB_2_eyeSleep_15x10[EYE_SIZE];
extern const uint8_t CUB_2_eyeSick_15x10[EYE_SIZE];
extern const uint8_t CUB_2_eyeDead_15x10[EYE_SIZE];

extern const uint8_t CUB_3_eyeIdle1_15x10[EYE_SIZE];
extern const uint8_t CUB_3_eyeIdle2_15x10[EYE_SIZE];
extern const uint8_t CUB_3_eyeHappy_15x10[EYE_SIZE];
extern const uint8_t CUB_3_eyeSleep_15x10[EYE_SIZE];
extern const uint8_t CUB_3_eyeSick_15x10[EYE_SIZE];
extern const uint8_t CUB_3_eyeDead_15x10[EYE_SIZE];

extern const uint8_t CUB_1_eyeAtk_15x10[EYE_SIZE];
extern const uint8_t CUB_2_eyeAtk_15x10[EYE_SIZE];
extern const uint8_t CUB_3_eyeAtk_15x10[EYE_SIZE];

extern const uint8_t CUB_1_attack_50x50[BODY_SIZE];
extern const uint8_t CUB_2_attack_50x50[BODY_SIZE];
extern const uint8_t CUB_3_attack_50x50[BODY_SIZE];

extern const uint8_t CUB_1_att_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_2_att_mask_50x50[BODY_SIZE];
extern const uint8_t CUB_3_att_mask_50x50[BODY_SIZE];

// Инкубатор
#define INCUBATOR_W 48
#define INCUBATOR_H 48
#define CRACK_W 27
#define CRACK_H 40
#define CRACK_SIZE ((CRACK_W + 7) / 8 * CRACK_H)
extern const uint8_t inc_crack_27x40[CRACK_SIZE];
extern const uint8_t inc_crack1_27x40[CRACK_SIZE];
extern const uint8_t inc_crack2_27x40[CRACK_SIZE];
extern const uint8_t inc_crack3_27x40[CRACK_SIZE];

#define CUBE_W 20
#define CUBE_H 20
#define CUBE_SIZE ((CUBE_W + 7) / 8 * CUBE_H)
extern const uint8_t cub_inc_20x20[CUBE_SIZE];
extern const uint8_t cub_inc2_20x20[CUBE_SIZE];

// Эмотиконы (15x15)
#define EMOTE_W 15
#define EMOTE_H 15
#define EMOTE_SIZE ((EMOTE_W + 7) / 8 * EMOTE_H)
extern const uint8_t emoteHappy_15x15[EMOTE_SIZE];
extern const uint8_t emoteHeart_15x15[EMOTE_SIZE];
extern const uint8_t emoteHungry_15x15[EMOTE_SIZE];
extern const uint8_t emoteSad_15x15[EMOTE_SIZE];
extern const uint8_t emoteSick_15x15[EMOTE_SIZE];
extern const uint8_t emoteSleep_15x15[EMOTE_SIZE];
extern const uint8_t emoteArrowUp_15x15[EMOTE_SIZE];
extern const uint8_t emoteGame_15x15[EMOTE_SIZE];
extern const uint8_t emoteClean_15x15[EMOTE_SIZE];
extern const uint8_t emoteDead_15x15[EMOTE_SIZE];

// Анимация поиска/загрузки (15x15)
extern const uint8_t seek1_15x15[EMOTE_SIZE];
extern const uint8_t seek2_15x15[EMOTE_SIZE];
extern const uint8_t seek3_15x15[EMOTE_SIZE];
extern const uint8_t seek4_15x15[EMOTE_SIZE];

// Заставка (128x64)
#define LOGO_W 128
#define LOGO_H 64
#define LOGO_SIZE ((LOGO_W + 7) / 8 * LOGO_H)
extern const uint8_t logo_1_128x64[LOGO_SIZE];
extern const uint8_t logo_2_128x64[LOGO_SIZE];
extern const uint8_t logo_3_128x64[LOGO_SIZE];

// Zzz спрайты (10x14)
#define ZZZ_W 10
#define ZZZ_H 14
#define ZZZ_SIZE ((ZZZ_W + 7) / 8 * ZZZ_H)
extern const uint8_t z1_10x14[ZZZ_SIZE];
extern const uint8_t z1_mask_10x14[ZZZ_SIZE];
extern const uint8_t z2_10x14[ZZZ_SIZE];
extern const uint8_t z2_mask_10x14[ZZZ_SIZE];
extern const uint8_t z3_10x14[ZZZ_SIZE];
extern const uint8_t z3_mask_10x14[ZZZ_SIZE];

// Стрелка вверх (7x7)
#define ARROWUP_W 7
#define ARROWUP_H 7
#define ARROWUP_SIZE ((ARROWUP_W + 7) / 8 * ARROWUP_H)
extern const uint8_t arrowUp_7x7[ARROWUP_SIZE];

// Иконки в меню (25x25)
#define MENU_W 25
#define MENU_H 25
#define MENU_SIZE ((MENU_W + 7) / 8 * MENU_H)
extern const uint8_t m_feed_25x25[MENU_SIZE];
extern const uint8_t m_play_25x25[MENU_SIZE];
extern const uint8_t m_sleep_25x25[MENU_SIZE];
extern const uint8_t m_clean_25x25[MENU_SIZE];
extern const uint8_t m_heal_25x25[MENU_SIZE];
extern const uint8_t m_evolve_25x25[MENU_SIZE];
extern const uint8_t m_battle_25x25[MENU_SIZE];
extern const uint8_t m_exit_25x25[MENU_SIZE];

// Боковые иконки (20x20)
#define MENU_SMALL_W 20
#define MENU_SMALL_H 20
#define MENU_SMALL_SIZE ((MENU_SMALL_W + 7) / 8 * MENU_SMALL_H)
extern const uint8_t m_feed__20x20[MENU_SMALL_SIZE];
extern const uint8_t m_play__20x20[MENU_SMALL_SIZE];
extern const uint8_t m_sleep__20x20[MENU_SMALL_SIZE];
extern const uint8_t m_clean__20x20[MENU_SMALL_SIZE];
extern const uint8_t m_heal__20x20[MENU_SMALL_SIZE];
extern const uint8_t m_evolve__20x20[MENU_SMALL_SIZE];
extern const uint8_t m_battle__20x20[MENU_SMALL_SIZE];
extern const uint8_t m_exit__20x20[MENU_SMALL_SIZE];

// Фоновые картинки (128x64)
#define BG_W 128
#define BG_H 64
#define BG_SIZE ((BG_W + 7) / 8 * BG_H)
#define BG_BLANK  ((const uint8_t*)0)
#define BG_FILL   ((const uint8_t*)1)
extern const uint8_t bg1_128x64[BG_SIZE];
extern const uint8_t bg2_128x64[BG_SIZE];
extern const uint8_t bg3_128x64[BG_SIZE];
extern const uint8_t bg4_128x64[BG_SIZE];
extern const uint8_t bg5_128x64[BG_SIZE];
extern const uint8_t bg6_128x64[BG_SIZE];
extern const uint8_t bg_sel_128x64[BG_SIZE];
extern const uint8_t bg_inc_128x64[BG_SIZE];
