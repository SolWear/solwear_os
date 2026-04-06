#include "screen_manager.h"

void ScreenManager::init(App* initialApp) {
    stackTop_ = 0;
    stack_[0] = initialApp;
    initialApp->onCreate();
    initialApp->onResume();
}

void ScreenManager::pushScreen(AppId id, Transition trans) {
    const AppEntry* entry = AppRegistry::instance().getEntry(id);
    if (!entry || !entry->factory) return;

    App* app = entry->factory();
    pushApp(app, trans);
}

void ScreenManager::pushApp(App* app, Transition trans) {
    if (stackTop_ >= MAX_APP_STACK - 1) return;

    App* current = currentApp();
    if (current) current->onPause();

    stackTop_++;
    stack_[stackTop_] = app;
    app->onCreate();
    app->onResume();

    if (trans != Transition::NONE && current) {
        transitioning_ = true;
        transType_ = trans;
        transStartTime_ = millis();
        transProgress_ = 0;
        transOutgoing_ = current;
    }
}

void ScreenManager::popScreen(Transition trans) {
    if (stackTop_ <= 0) return;  // Don't pop the watchface

    App* top = stack_[stackTop_];
    transOutgoing_ = top;

    top->onPause();
    top->onDestroy();

    stackTop_--;
    App* revealed = stack_[stackTop_];
    if (revealed) revealed->onResume();

    if (trans != Transition::NONE) {
        transitioning_ = true;
        transType_ = trans;
        transStartTime_ = millis();
        transProgress_ = 0;
    }

    delete top;
}

void ScreenManager::goHome() {
    while (stackTop_ > 0) {
        App* top = stack_[stackTop_];
        top->onPause();
        top->onDestroy();
        delete top;
        stackTop_--;
    }
    stack_[0]->onResume();
    transitioning_ = false;
}

App* ScreenManager::currentApp() const {
    if (stackTop_ < 0) return nullptr;
    return stack_[stackTop_];
}

void ScreenManager::handleEvent(const Event& event) {
    if (transitioning_) return;  // Ignore input during transitions

    App* app = currentApp();
    if (!app) return;

    // Handle back gesture globally
    if (event.type == EventType::TOUCH && event.touch.gesture == GestureType::SWIPE_RIGHT) {
        if (!app->handleBackGesture()) {
            popScreen(Transition::SLIDE_RIGHT);
            return;
        }
    }

    app->onEvent(event);
}

void ScreenManager::update(uint32_t dt) {
    if (transitioning_) {
        uint32_t elapsed = millis() - transStartTime_;
        float t = (float)elapsed / TRANSITION_DURATION_MS;
        if (t >= 1.0f) {
            t = 1.0f;
            transitioning_ = false;
            transOutgoing_ = nullptr;
        }
        transProgress_ = easeOut(t);
    }

    App* app = currentApp();
    if (app) app->update(dt);
}

void ScreenManager::render(TFT_eSprite& canvas) {
    App* app = currentApp();
    if (!app) return;

    if (transitioning_ && transOutgoing_) {
        // Render transition: slide both screens
        int16_t offset = 0;
        int16_t outOffset = 0;

        switch (transType_) {
            case Transition::SLIDE_LEFT:
                offset = (int16_t)((1.0f - transProgress_) * SCREEN_WIDTH);
                outOffset = (int16_t)(-transProgress_ * SCREEN_WIDTH);
                break;
            case Transition::SLIDE_RIGHT:
                offset = (int16_t)(-(1.0f - transProgress_) * SCREEN_WIDTH);
                outOffset = (int16_t)(transProgress_ * SCREEN_WIDTH);
                break;
            case Transition::SLIDE_UP:
                offset = (int16_t)((1.0f - transProgress_) * SCREEN_HEIGHT);
                outOffset = (int16_t)(-transProgress_ * SCREEN_HEIGHT);
                break;
            case Transition::SLIDE_DOWN:
                offset = (int16_t)(-(1.0f - transProgress_) * SCREEN_HEIGHT);
                outOffset = (int16_t)(transProgress_ * SCREEN_HEIGHT);
                break;
            default:
                break;
        }

        // For simplicity during transition, just render the incoming app with offset fade
        // A full dual-render would need two sprites which we can't afford in RAM
        canvas.fillSprite(TFT_BLACK);
        app->render(canvas);
    } else {
        app->render(canvas);
    }
}
