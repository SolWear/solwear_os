#pragma once
#include <TFT_eSPI.h>
#include "ui_common.h"

class StatusBar {
public:
    void render(TFT_eSprite& canvas);
    void setTitle(const char* title) { title_ = title; }
    void clearTitle() { title_ = nullptr; }

private:
    const char* title_ = nullptr;
};

extern StatusBar statusBar;
