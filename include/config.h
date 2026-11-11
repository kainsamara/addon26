#pragma once

#include <Arduino.h>

// ================= HARDWARE =================

#define BTN_LEFT            10
#define BTN_OK              9
#define BTN_RIGHT           8

#define INPUT_LOCK_MS       500     // Блок ввода после смены экрана
#define BTN_LONG_PRESS_MS   800     // Задержка длинного нажатия
#define BTN_HOLD_MS         BTN_LONG_PRESS_MS
#define BTN_REPEAT_MS       500     // Задержка до повтора зажатой кнопки
#define BTN_OK_CONFIRM_MS   BTN_LONG_PRESS_MS  

#define BUZZER_PIN          2

#define OLED_WIDTH          128
#define OLED_HEIGHT         64
#define OLED_CS             5
#define OLED_DC             4
#define OLED_MOSI           6
#define OLED_SCK            7
#define OLED_RST            1

#define NEOPIXEL_PIN        3
#define NEOPIXEL_COUNT      1
#define NEOPIXEL_BRIGHTNESS 255     // 0-255, общий мастер-уровень (равномерно все каналы)
#define NEOPIXEL_MAX_R      255     // Поканальный максимум яркости (0-255) — калибровка/баланс белого, если каналы
#define NEOPIXEL_MAX_G      220     // светодиода неравны (напр. синий слишком яркий). 255 = без ограничения.
#define NEOPIXEL_MAX_B      240     // Применяется к КАЖДОМУ цвету, включая белый и фоновую подсветку.
#define AMBIENT_BRIGHTNESS  240     // Максимум подсветки на клавном экране 
#define ACTIVITY_TIMEOUT    25000   // Таймаут подсветки

#define IDLE_SLEEP_ENABLE   1       // 1 = засыпать в простое (экономия батареи), 0 = отключить сон
#define IDLE_SLEEP_MAX_MS   250     // потолок сна: кнопки на GPIO8-10 НЕ в RTC-домене C3 (RTC = GPIO0-5) и могут
                                    // не будить из light sleep. 250 мс держат отклик кнопек незаметным даже если
                                    // GPIO-wakeup не сработает и пет просыпается только по таймеру.
#define FPS_LOCK            24      // FPS активного экрана (кадр = 1000/FPS_LOCK мс)
#define DISPLAY_CONTRAST    48      // контраст ST7567 (0-63)
#define DISPLAY_ROTATE      1

// ================= PET =================

#define PET1_DEFAULT_NAME   "CUB_3_3"
#define PET2_DEFAULT_NAME   "CUB_3_4"
#define PET3_DEFAULT_NAME   "CUB_3_2"
#define PET_MAX_NAME        8
#define PET_MAX_STAT        100
#define PET_SAVE_INTERVAL   30000
#define PET_UPDATE_RATE     5000
#define PET_AGE_INTERVAL    3600000  // +1 день жизни за 1 реальный час
#define LEVEL_START_XP      50
#define EVOLUTION_LEVEL     5
#define MAX_LEVEL           255
#define INCUBATOR_TIME      600000   // 10 минут
#define AUTO_AWAKE          0

#define WALK_DIST_MIN       16
#define WALK_DIST_MAX       48
#define WALK_PAUSE_MIN      1500
#define WALK_PAUSE_MAX      4000

// ================= GAME =================

#define DECAY_HUNGER_MULT       6.0f
#define DECAY_HAPPINESS_MULT    5.0f
#define DECAY_ENERGY_MULT       8.0f
#define DECAY_CLEANLINESS_MULT  7.0f
#define SLEEP_HUNGER_MULT       12.0f   // голод во сне тикает вдвое медленнее бодрствования, но НЕ стоит на нуле — вечный сон не бессмертие
#define SLEEP_RECOVERY_RATE     5
#define XP_MULTIPLIER           3.0f
#define XP_FEED                 3
#define XP_PLAY                 5
#define XP_CLEAN                6
#define XP_HEAL                 4
#define XP_EVOLVE               50
#define XP_QUICK                2
#define XP_BATTLE_WIN           30
#define XP_BATTLE_LOSE          15
#define XP_BATTLE_DRAW          20
#define BATTLE_SAFE_PERIOD      10000   // 10сек после боя без падения характеристик чтобы пет не откис пока лечишь

// ================= UI =================

#define UI_BOOTLOGO_TIME        2000
#define UI_BLINK_INTERVAL       3500
#define UI_BLINK_DURATION       120

// BG_BLANK для пустого белого, BG_FILL для черного.
// SCREEN_MAIN юзает BG_MAIN_BABY для первой формы кубика и BG_MAIN_ADULT для эволюции.
#define BG_SELECT_TYPE      bg_sel_128x64
#define BG_ENTER_NAME       BG_BLANK
#define BG_MAIN_BABY        bg1_128x64
#define BG_MAIN_ADULT       bg2_128x64
#define BG_STATS            BG_BLANK
#define BG_MENU             BG_BLANK
#define BG_INFO             BG_BLANK
#define BG_EVOLUTION        BG_BLANK
#define BG_DEAD             BG_BLANK
#define BG_HATCHING         bg_inc_128x64
#define BG_CONFIRM_RESET    BG_BLANK
#define BG_CONFIRM_NAME     BG_BLANK
#define BG_BATTLE_SEARCH    BG_BLANK
#define BG_BATTLE           BG_BLANK
#define BG_BATTLE_WIN       BG_BLANK
#define BG_BATTLE_LOSE      BG_BLANK
#define BG_BATTLE_DRAW      BG_BLANK
#define BG_LOW_HP           BG_BLANK
#define BG_WELCOME          BG_BLANK