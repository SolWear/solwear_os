#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "config.h"

class HalEink {
public:
    void init();
    bool isReady() const { return ready_; }

    uint16_t width() const { return WIDTH; }
    uint16_t height() const { return HEIGHT; }

    void beginFrame(bool white = true);
    void present(bool antiGhost = true);
    void scrub();
    void drawPixel(int16_t x, int16_t y, bool black = true);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool black = true);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, bool black = true);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool black = true);
    void drawCircle(int16_t x0, int16_t y0, int16_t r, bool black = true);

    void clear(bool white = true);
    void drawTestPattern();

private:
    void sendCommand(uint8_t cmd);
    void sendData(uint8_t data);
    void waitBusy(const char* stage);
    void setRamWindow();
    void setRamPointer();
    void refresh();
    void writeRam(uint8_t cmd, const uint8_t* data);
    void writeSolidRam(uint8_t cmd, uint8_t value);

    static constexpr uint16_t WIDTH = 200;
    static constexpr uint16_t HEIGHT = 200;
    static constexpr uint16_t BUF_SIZE = (WIDTH * HEIGHT) / 8;

    bool ready_ = false;
    uint8_t frameBuf_[BUF_SIZE] = {0xFF};
    uint32_t presentCount_ = 0;
};

extern HalEink eink;
