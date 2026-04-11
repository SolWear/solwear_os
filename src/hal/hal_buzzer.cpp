#include "hal_buzzer.h"

HalBuzzer buzzer;

static const Note melodyClick[] = {{SND_CLICK_FREQ, SND_CLICK_DUR}};
static const Note melodyBeep[] = {{SND_BEEP_FREQ, SND_BEEP_DUR}, {0, 50}, {SND_BEEP_FREQ, SND_BEEP_DUR}};
static const Note melodySuccess[] = {{1000, 80}, {1500, 80}, {2000, 120}};
static const Note melodyError[] = {{500, 150}, {300, 200}};
static const Note melodyAlarm[] = {
    {SND_ALARM_FREQ_LO, 200}, {SND_ALARM_FREQ_HI, 200},
    {SND_ALARM_FREQ_LO, 200}, {SND_ALARM_FREQ_HI, 200},
    {0, 400}
};

void HalBuzzer::init() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
}

void HalBuzzer::tone(uint16_t freqHz, uint16_t durationMs) {
    if (!enabled_) return;
    if (freqHz > 0) {
        ::tone(PIN_BUZZER, freqHz);
    } else {
        ::noTone(PIN_BUZZER);
    }
    noteStartTime_ = millis();
}

void HalBuzzer::noTone() {
    ::noTone(PIN_BUZZER);
    digitalWrite(PIN_BUZZER, LOW);
    playing_ = false;
}

void HalBuzzer::playMelody(const Note* notes, uint8_t count) {
    if (!enabled_) return;
    melody_ = notes;
    melodyLen_ = count;
    melodyIdx_ = 0;
    playing_ = true;
    tone(melody_[0].freq, melody_[0].duration);
}

void HalBuzzer::update() {
    if (!playing_ || !melody_) return;

    uint32_t elapsed = millis() - noteStartTime_;
    if (elapsed >= melody_[melodyIdx_].duration) {
        melodyIdx_++;
        if (melodyIdx_ >= melodyLen_) {
            noTone();
            melody_ = nullptr;
            return;
        }
        tone(melody_[melodyIdx_].freq, melody_[melodyIdx_].duration);
    }
}

void HalBuzzer::click()   { playMelody(melodyClick, 1); }
void HalBuzzer::beep()    { playMelody(melodyBeep, 3); }
void HalBuzzer::success() { playMelody(melodySuccess, 3); }
void HalBuzzer::error()   { playMelody(melodyError, 2); }
void HalBuzzer::alarm()   { playMelody(melodyAlarm, 5); }
