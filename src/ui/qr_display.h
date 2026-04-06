#pragma once
#include <TFT_eSPI.h>
#include "ui_common.h"

class QrDisplay {
public:
    // Render a QR code centered at (cx, cy) with given max size
    static void render(TFT_eSprite& canvas, const char* data,
                       int16_t cx, int16_t cy, uint16_t maxSize,
                       uint16_t fgColor = Theme::TEXT_PRIMARY,
                       uint16_t bgColor = Theme::BG_PRIMARY);
};
