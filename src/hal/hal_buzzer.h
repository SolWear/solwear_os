#pragma once
#include <Arduino.h>
#include "config.h"

struct Note {
    uint16_t freq;
    uint16_t duration;
};

class HalBuzzer {
public:
    void init();
    void tone(uint16_t freqHz, uint16_t durationMs);
    void noTone();
    void playMelody(const Note* notes, uint8_t count);
    void update();

    void click();
    void beep();
    void success();
    void error();
    void alarm();

    void setEnabled(bool en) { enabled_ = en; }
    bool isEnabled() const { return enabled_; }
    bool isPlaying() const { return playing_; }

private:
    bool enabled_ = true;
    bool playing_ = false;
    const Note* melody_ = nullptr;
    uint8_t melodyLen_ = 0;
    uint8_t melodyIdx_ = 0;
    uint32_t noteStartTime_ = 0;
};

extern HalBuzzer buzzer;
