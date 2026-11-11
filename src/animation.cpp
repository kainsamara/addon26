#include "animation.h"

#include "pet.h"
#include "sprites.h"
#include "config.h"
#include "ui.h"

static uint32_t lastBlink = 0;
static uint32_t blinkStart = 0;
static bool blinking = false;

bool animationBlinking()
{
    return blinking;
}

void animationUpdate()
{
    uint32_t now = millis();

    if (!blinking)
    {
        if (now - lastBlink > UI_BLINK_INTERVAL)
        {
            blinking = true;
            blinkStart = now;
            lastBlink = now;
        }
    }
    else
    {
        if (now - blinkStart > UI_BLINK_DURATION)
        {
            blinking = false;
        }
    }
}

uint8_t animationCurrentFace()
{
    if (pet.sleeping) return FACE_SLEEP;

    uint8_t active = uiGetCurrentEmote();
    switch (active)
    {
        case EMOTION_HAPPY:
        case EMOTE_FED:
        case EMOTE_HEALED:      return FACE_HAPPY;
        case EMOTION_SLEEPY:    return FACE_SLEEP;
        case EMOTION_SICK:
        case EMOTE_NEED_FOOD:
        case EMOTE_NEED_CLEAN:
        case EMOTE_NEED_PLAY:
        case EMOTE_NEED_ENERGY: return FACE_SICK;
        case EMOTION_SAD:
        case EMOTION_HUNGRY:    return FACE_SAD;
        default: break;
    }

    return blinking ? FACE_IDLE2 : FACE_IDLE1;
}
