#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "event_system.h"

// Forward declaration
class App;

// Transition types for screen navigation
enum class Transition : uint8_t {
    NONE,
    SLIDE_LEFT,
    SLIDE_RIGHT,
    SLIDE_UP,
    SLIDE_DOWN
};

// Base class for all apps
class App {
public:
    virtual ~App() {}

    virtual void onCreate() {}
    virtual void onResume() {}
    virtual void onPause() {}
    virtual void onDestroy() {}
    virtual void onEvent(const Event& event) {}
    virtual void update(uint32_t dt) {}
    virtual void render(TFT_eSprite& canvas) = 0;

    virtual bool wantsStatusBar() { return true; }
    virtual bool handleBackGesture() { return false; }
    virtual AppId getAppId() const = 0;
};

// Factory function type for creating apps
typedef App* (*AppFactory)();

// Registry entry for an app
struct AppEntry {
    AppId id;
    const char* name;
    const uint16_t* icon;    // Pointer to PROGMEM icon data (48x48 RGB565)
    AppFactory factory;
};

// App Registry — stores metadata for all installed apps
class AppRegistry {
public:
    static AppRegistry& instance() {
        static AppRegistry inst;
        return inst;
    }

    void registerApp(AppId id, const char* name, const uint16_t* icon, AppFactory factory);
    const AppEntry* getEntry(AppId id) const;
    const AppEntry* getEntries() const { return entries_; }
    uint8_t getCount() const { return count_; }

private:
    AppRegistry() : count_(0) {}
    AppEntry entries_[APP_COUNT];
    uint8_t count_;
};
