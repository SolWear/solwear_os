#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "app_framework.h"
#include "event_system.h"

class ScreenManager {
public:
    static ScreenManager& instance() {
        static ScreenManager inst;
        return inst;
    }

    void init(App* initialApp);
    void pushScreen(AppId id, Transition trans = Transition::SLIDE_LEFT);
    void pushApp(App* app, Transition trans = Transition::SLIDE_LEFT);
    void popScreen(Transition trans = Transition::SLIDE_RIGHT);
    void goHome();

    void handleEvent(const Event& event);
    void update(uint32_t dt);
    void render(TFT_eSprite& canvas);

    App* currentApp() const;
    uint8_t stackDepth() const { return stackTop_ + 1; }
    bool isTransitioning() const { return transitioning_; }

private:
    ScreenManager() : stackTop_(-1) {}

    // Ease-out cubic: 1 - (1-t)^3
    float easeOut(float t) {
        float inv = 1.0f - t;
        return 1.0f - inv * inv * inv;
    }

    App* stack_[MAX_APP_STACK] = {};
    int8_t stackTop_ = -1;

    // Transition state
    bool transitioning_ = false;
    Transition transType_ = Transition::NONE;
    uint32_t transStartTime_ = 0;
    float transProgress_ = 0;
    App* transOutgoing_ = nullptr;
};
