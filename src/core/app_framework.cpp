#include "app_framework.h"

void AppRegistry::registerApp(AppId id, const char* name, const uint16_t* icon, AppFactory factory) {
    if (count_ >= APP_COUNT) return;
    entries_[count_].id = id;
    entries_[count_].name = name;
    entries_[count_].icon = icon;
    entries_[count_].factory = factory;
    count_++;
}

const AppEntry* AppRegistry::getEntry(AppId id) const {
    for (uint8_t i = 0; i < count_; i++) {
        if (entries_[i].id == id) return &entries_[i];
    }
    return nullptr;
}
