#include "hal_eink.h"
#include <string.h>

HalEink eink;

void HalEink::sendCommand(uint8_t cmd) {
    digitalWrite(PIN_EINK_DC, LOW);
    digitalWrite(PIN_EINK_CS, LOW);
    SPI.transfer(cmd);
    digitalWrite(PIN_EINK_CS, HIGH);
}

void HalEink::sendData(uint8_t data) {
    digitalWrite(PIN_EINK_DC, HIGH);
    digitalWrite(PIN_EINK_CS, LOW);
    SPI.transfer(data);
    digitalWrite(PIN_EINK_CS, HIGH);
}

void HalEink::waitBusy(const char* stage) {
    const uint32_t start = millis();
    while (digitalRead(PIN_EINK_BUSY) == HIGH) {
        if (millis() - start > 4000) {
            Serial.printf("[EINK] busy timeout at %s\n", stage);
            return;
        }
        delay(10);
    }
}

void HalEink::setRamWindow() {
    // X address from 0 to 24 (200 / 8 - 1)
    sendCommand(0x44);
    sendData(0x00);
    sendData((WIDTH / 8) - 1);

    // Y address from 0 to 199
    sendCommand(0x45);
    sendData((HEIGHT - 1) & 0xFF);
    sendData(((HEIGHT - 1) >> 8) & 0xFF);
    sendData(0x00);
    sendData(0x00);
}

void HalEink::setRamPointer() {
    sendCommand(0x4E);
    sendData(0x00);

    sendCommand(0x4F);
    sendData((HEIGHT - 1) & 0xFF);
    sendData(((HEIGHT - 1) >> 8) & 0xFF);
    waitBusy("set-ram-pointer");
}

void HalEink::refresh(bool fastRefresh) {
    sendCommand(0x22);
    sendData(fastRefresh ? 0xCF : 0xC7);
    sendCommand(0x20);
    waitBusy("refresh");
}

void HalEink::writeRam(uint8_t cmd, const uint8_t* data) {
    sendCommand(cmd);
    for (uint16_t i = 0; i < BUF_SIZE; ++i) {
        sendData(data[i]);
    }
}

void HalEink::writeSolidRam(uint8_t cmd, uint8_t value) {
    sendCommand(cmd);
    for (uint16_t i = 0; i < BUF_SIZE; ++i) {
        sendData(value);
    }
}

void HalEink::beginFrame(bool white) {
    memset(frameBuf_, white ? 0xFF : 0x00, sizeof(frameBuf_));
}

void HalEink::drawPixel(int16_t x, int16_t y, bool black) {
#if SOLWEAR_EINK_ROTATE_180
    x = (int16_t)(WIDTH - 1 - x);
    y = (int16_t)(HEIGHT - 1 - y);
#endif
    if (x < 0 || y < 0 || x >= (int16_t)WIDTH || y >= (int16_t)HEIGHT) {
        return;
    }
    const uint16_t index = (uint16_t)y * (WIDTH / 8) + (uint16_t)(x / 8);
    const uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    if (black) {
        frameBuf_[index] &= (uint8_t)~mask;
    } else {
        frameBuf_[index] |= mask;
    }
}

void HalEink::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool black) {
    int16_t dx = abs(x1 - x0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0);
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while (true) {
        drawPixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void HalEink::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, bool black) {
    if (w <= 0 || h <= 0) {
        return;
    }
    drawLine(x, y, x + w - 1, y, black);
    drawLine(x, y + h - 1, x + w - 1, y + h - 1, black);
    drawLine(x, y, x, y + h - 1, black);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1, black);
}

void HalEink::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool black) {
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int16_t yy = y; yy < y + h; ++yy) {
        drawLine(x, yy, x + w - 1, yy, black);
    }
}

void HalEink::drawCircle(int16_t x0, int16_t y0, int16_t r, bool black) {
    if (r <= 0) {
        return;
    }

    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        drawPixel(x0 + x, y0 + y, black);
        drawPixel(x0 + y, y0 + x, black);
        drawPixel(x0 - y, y0 + x, black);
        drawPixel(x0 - x, y0 + y, black);
        drawPixel(x0 - x, y0 - y, black);
        drawPixel(x0 - y, y0 - x, black);
        drawPixel(x0 + y, y0 - x, black);
        drawPixel(x0 + x, y0 - y, black);

        if (err <= 0) {
            y++;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void HalEink::present(bool antiGhost, bool fastRefresh) {
    if (!ready_) return;

    if (antiGhost && presentCount_ > 0 && (presentCount_ % 6 == 0)) {
        scrub();
    }

    setRamWindow();
    setRamPointer();

    // Keep both internal controller memories in sync to avoid random block
    // artifacts on subsequent refreshes.
    writeRam(0x24, frameBuf_);
    writeRam(0x26, frameBuf_);

    refresh(fastRefresh && !antiGhost);
    presentCount_++;
}

void HalEink::scrub() {
    if (!ready_) return;

    // Black -> White cleanup reduces ghosting on partial-content updates.
    setRamWindow();
    setRamPointer();
    writeSolidRam(0x24, 0x00);
    writeSolidRam(0x26, 0x00);
    refresh();

    setRamWindow();
    setRamPointer();
    writeSolidRam(0x24, 0xFF);
    writeSolidRam(0x26, 0xFF);
    refresh();

    memset(frameBuf_, 0xFF, sizeof(frameBuf_));
}

void HalEink::init() {
#if !SOLWEAR_EINK_TARGET
    ready_ = false;
    return;
#else
    pinMode(PIN_EINK_CS, OUTPUT);
    pinMode(PIN_EINK_DC, OUTPUT);
    pinMode(PIN_EINK_RST, OUTPUT);
    pinMode(PIN_EINK_BUSY, INPUT);

    digitalWrite(PIN_EINK_CS, HIGH);
    digitalWrite(PIN_EINK_DC, HIGH);

    // E-ink is write-only for this driver, but ESP32 SPI.begin expects a
    // valid MISO pin to avoid gpio warnings. Reuse BUSY as an input pin here.
    SPI.begin(PIN_EINK_CLK, PIN_EINK_BUSY, PIN_EINK_DIN, PIN_EINK_CS);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));

    // Hardware reset
    digitalWrite(PIN_EINK_RST, HIGH);
    delay(20);
    digitalWrite(PIN_EINK_RST, LOW);
    delay(10);
    digitalWrite(PIN_EINK_RST, HIGH);
    delay(20);

    // Software reset
    sendCommand(0x12);
    waitBusy("sw-reset");

    // Driver output control for 200 rows.
    sendCommand(0x01);
    sendData(0xC7);
    sendData(0x00);
    sendData(0x01);

    // Data entry mode: X+, Y+
    sendCommand(0x11);
    sendData(0x01);

    setRamWindow();
    setRamPointer();

    // Border waveform and built-in temperature load.
    sendCommand(0x3C);
    sendData(0x01);
    sendCommand(0x18);
    sendData(0x80);
    sendCommand(0x22);
    sendData(0xB1);
    sendCommand(0x20);
    waitBusy("temp-load");

    // Force a white baseline on both controller memories so first UI render
    // doesn't inherit undefined garbage from power-on state.
    setRamWindow();
    setRamPointer();
    writeSolidRam(0x24, 0xFF);
    writeSolidRam(0x26, 0xFF);
    refresh();
    presentCount_ = 0;

    ready_ = true;
    Serial.println("[EINK] init done (stable full-refresh mode)");
#endif
}

void HalEink::clear(bool white) {
#if !SOLWEAR_EINK_TARGET
    return;
#else
    if (!ready_) return;

    beginFrame(white);
    setRamWindow();
    setRamPointer();
    writeRam(0x24, frameBuf_);
    writeRam(0x26, frameBuf_);
    refresh();
    presentCount_ = 0;
    Serial.printf("[EINK] clear %s\n", white ? "white" : "black");
#endif
}

void HalEink::drawTestPattern() {
#if !SOLWEAR_EINK_TARGET
    return;
#else
    if (!ready_) return;

    beginFrame(true);
    for (uint16_t y = 0; y < HEIGHT; ++y) {
        for (uint16_t xb = 0; xb < WIDTH / 8; ++xb) {
            for (uint8_t bit = 0; bit < 8; ++bit) {
                const uint16_t x = xb * 8 + bit;
                const bool white = ((x / 16) + (y / 16)) % 2 == 0;
                drawPixel((int16_t)x, (int16_t)y, !white);
            }
        }
    }

    present();
    Serial.println("[EINK] checkerboard test pushed");
#endif
}
