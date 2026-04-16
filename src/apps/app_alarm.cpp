#include "app_alarm.h"
#include "../core/screen_manager.h"
#include "../core/system_clock.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"
#include "../hal/hal_buzzer.h"
#include "../assets/sounds.h"

void AlarmApp::onCreate() {
    statusBar.setTitle("Alarm");
}

void AlarmApp::onDestroy() {
    statusBar.clearTitle();
}

void AlarmApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

    // Dismiss alarm if firing
    if (alarmFiring_) {
        if (event.touch.gesture == GestureType::TAP) {
            alarmFiring_ = false;
            buzzer.noTone();
        }
        return;
    }

#if SOLWEAR_HAS_BUTTONS
    // K3=back always exits; K4=TAP cycles views; K1/K2 are contextual per view
    if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
        return;
    }
    if (event.touch.gesture == GestureType::TAP) {
        view_ = (view_ + 1) % 2; buzzer.click();
        return;
    }
    if (view_ == 0) {
        if (event.touch.gesture == GestureType::SWIPE_DOWN) {
            alarmHour_ = (alarmHour_ + 1) % 24; buzzer.click();
        } else if (event.touch.gesture == GestureType::SWIPE_UP) {
            alarmMin_ = (alarmMin_ + 5) % 60; buzzer.click();
        }
    } else {
        if (event.touch.gesture == GestureType::SWIPE_UP) {
            if (!swRunning_) { swRunning_ = true; swStartTime_ = millis() - swElapsed_; }
            else { swRunning_ = false; swElapsed_ = millis() - swStartTime_; }
            buzzer.click();
        } else if (event.touch.gesture == GestureType::SWIPE_DOWN) {
            if (swRunning_) {
                if (swLapCount_ < 5) swLaps_[swLapCount_++] = millis() - swStartTime_;
            } else {
                swElapsed_ = 0; swLapCount_ = 0;
            }
            buzzer.click();
        }
    }
#else
    // Switch views
    if (event.touch.gesture == GestureType::SWIPE_LEFT && view_ < 1) {
        view_++;
        return;
    }
    if (event.touch.gesture == GestureType::SWIPE_RIGHT && view_ > 0) {
        view_--;
        return;
    }

    if (view_ == 0) {
        // Alarm set view
        if (event.touch.gesture == GestureType::TAP) {
            int16_t tx = event.touch.x;
            int16_t ty = event.touch.y;

            // Hour area (left half, upper)
            if (tx < SCREEN_WIDTH / 2 && ty > 80 && ty < 160) {
                alarmHour_ = (alarmHour_ + 1) % 24;
                buzzer.click();
            }
            // Minute area (right half, upper)
            else if (tx >= SCREEN_WIDTH / 2 && ty > 80 && ty < 160) {
                alarmMin_ = (alarmMin_ + 5) % 60;
                buzzer.click();
            }
            // Toggle button
            else if (ty >= 190 && ty < 240) {
                alarmEnabled_ = !alarmEnabled_;
                buzzer.click();
            }
        }
    } else {
        // Stopwatch view
        if (event.touch.gesture == GestureType::TAP) {
            int16_t ty = event.touch.y;

            if (ty >= 190 && ty < 230) {
                // Start/Stop button
                if (!swRunning_) {
                    swRunning_ = true;
                    swStartTime_ = millis() - swElapsed_;
                } else {
                    swRunning_ = false;
                    swElapsed_ = millis() - swStartTime_;
                }
                buzzer.click();
            } else if (ty >= 235) {
                if (swRunning_) {
                    // Lap
                    if (swLapCount_ < 5) {
                        swLaps_[swLapCount_++] = millis() - swStartTime_;
                    }
                } else {
                    // Reset
                    swElapsed_ = 0;
                    swLapCount_ = 0;
                }
                buzzer.click();
            }
        }
    }
#endif
}

void AlarmApp::update(uint32_t dt) {
    // Check alarm
    if (alarmEnabled_ && !alarmFiring_) {
        DateTime now = SystemClock::instance().now();
        if (now.hour == alarmHour_ && now.minute == alarmMin_ && now.second == 0) {
            alarmFiring_ = true;
            buzzer.playMelody(Sounds::ALARM, Sounds::ALARM_LEN);
        }
    }

    // Keep alarm ringing
    if (alarmFiring_ && !buzzer.isPlaying()) {
        buzzer.playMelody(Sounds::ALARM, Sounds::ALARM_LEN);
    }
}

void AlarmApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);
    statusBar.render(canvas);

    // Alarm firing overlay
    if (alarmFiring_) {
        canvas.fillSprite(Theme::DANGER);
        Draw::drawCenteredText(canvas, "ALARM!", 80, 4, Theme::TEXT_PRIMARY);
        char timeBuf[6];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", alarmHour_, alarmMin_);
        Draw::drawCenteredText(canvas, timeBuf, 140, 7, Theme::TEXT_PRIMARY);
        Draw::drawCenteredText(canvas, "Tap to dismiss", 220, 2, Theme::TEXT_PRIMARY);
        return;
    }

    if (view_ == 0) renderAlarmSet(canvas);
    else renderStopwatch(canvas);

    // View dots
    for (int i = 0; i < 2; i++) {
        int16_t dotX = SCREEN_WIDTH / 2 - 6 + i * 12;
        if (i == view_) {
            canvas.fillCircle(dotX, SCREEN_HEIGHT - 10, 3, Theme::ACCENT);
        } else {
            canvas.drawCircle(dotX, SCREEN_HEIGHT - 10, 3, Theme::TEXT_MUTED);
        }
    }
}

void AlarmApp::renderAlarmSet(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Set Alarm", APP_AREA_Y + 8, 2, Theme::TEXT_SECONDARY);

    // Time display
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", alarmHour_, alarmMin_);
    Draw::drawCenteredText(canvas, timeBuf, 90, 7, Theme::TEXT_PRIMARY);

#if SOLWEAR_HAS_BUTTONS
    canvas.setTextColor(Theme::TEXT_MUTED);
    canvas.drawString("K1:+1h", 20, 150, 1);
    int16_t tw = canvas.textWidth("K2:+5m", 1);
    canvas.drawString("K2:+5m", SCREEN_WIDTH - 20 - tw, 150, 1);
#else
    canvas.setTextColor(Theme::TEXT_MUTED);
    canvas.drawString("Tap: +1h", 20, 150, 1);
    int16_t tw = canvas.textWidth("Tap: +5m", 1);
    canvas.drawString("Tap: +5m", SCREEN_WIDTH - 20 - tw, 150, 1);
#endif

    // Enable/disable toggle
    uint16_t btnColor = alarmEnabled_ ? Theme::SUCCESS : Theme::BG_CARD;
    Draw::roundRect(canvas, 50, 190, 140, 36, Theme::CORNER_RADIUS, btnColor);
    const char* label = alarmEnabled_ ? "Enabled" : "Disabled";
    Draw::drawCenteredText(canvas, label, 200, 2, Theme::TEXT_PRIMARY);
}

void AlarmApp::renderStopwatch(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Stopwatch", APP_AREA_Y + 8, 2, Theme::TEXT_SECONDARY);

    // Elapsed time
    uint32_t elapsed = swRunning_ ? (millis() - swStartTime_) : swElapsed_;
    uint32_t mins = (elapsed / 60000) % 60;
    uint32_t secs = (elapsed / 1000) % 60;
    uint32_t centis = (elapsed / 10) % 100;

    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu", (unsigned long)mins, (unsigned long)secs);
    Draw::drawCenteredText(canvas, timeBuf, 70, 7, Theme::TEXT_PRIMARY);

    snprintf(timeBuf, sizeof(timeBuf), ".%02lu", (unsigned long)centis);
    Draw::drawCenteredText(canvas, timeBuf, 125, 4, Theme::ACCENT);

    // Laps
    for (uint8_t i = 0; i < swLapCount_; i++) {
        uint32_t lapMs = swLaps_[i];
        uint32_t lm = (lapMs / 60000) % 60;
        uint32_t ls = (lapMs / 1000) % 60;
        uint32_t lc = (lapMs / 10) % 100;
        char lapBuf[24];
        snprintf(lapBuf, sizeof(lapBuf), "Lap %d: %02lu:%02lu.%02lu",
                 i + 1, (unsigned long)lm, (unsigned long)ls, (unsigned long)lc);
        canvas.setTextColor(Theme::TEXT_SECONDARY);
        canvas.drawString(lapBuf, Theme::PADDING, 155 + i * 14, 1);
    }

    // Start/Stop button
    uint16_t btnColor = swRunning_ ? Theme::DANGER : Theme::SUCCESS;
    const char* btnLabel = swRunning_ ? "Stop" : "Start";
    Draw::roundRect(canvas, 30, 190, 80, 34, Theme::CORNER_RADIUS, btnColor);
    Draw::drawCenteredText(canvas, btnLabel, 200, 2, Theme::TEXT_PRIMARY);

    // Lap/Reset button
    const char* btn2Label = swRunning_ ? "Lap" : "Reset";
    Draw::roundRect(canvas, 130, 190, 80, 34, Theme::CORNER_RADIUS, Theme::BG_CARD);
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    int16_t btn2tw = canvas.textWidth(btn2Label, 2);
    canvas.drawString(btn2Label, 170 - btn2tw / 2, 200, 2);
}
