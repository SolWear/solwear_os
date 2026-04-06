#include "hal_touch.h"

HalTouch touch;

// CST816S register addresses
#define CST816S_REG_GESTURE   0x01
#define CST816S_REG_FINGER    0x02
#define CST816S_REG_XH        0x03
#define CST816S_REG_XL        0x04
#define CST816S_REG_YH        0x05
#define CST816S_REG_YL        0x06

void HalTouch::init() {
    // Touch shares I2C1 with IMU — Wire1 must be initialized before calling this
    // Reset touch controller
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(50);

    pinMode(PIN_TOUCH_INT, INPUT);
}

bool HalTouch::readRaw(int16_t& x, int16_t& y) {
    Wire1.beginTransmission(TOUCH_I2C_ADDR);
    Wire1.write(CST816S_REG_FINGER);
    if (Wire1.endTransmission() != 0) return false;

    Wire1.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)5);
    if (Wire1.available() < 5) return false;

    uint8_t fingers = Wire1.read();
    uint8_t xh = Wire1.read();
    uint8_t xl = Wire1.read();
    uint8_t yh = Wire1.read();
    uint8_t yl = Wire1.read();

    if (fingers == 0) return false;

    x = ((xh & 0x0F) << 8) | xl;
    y = ((yh & 0x0F) << 8) | yl;

    // Clamp to screen bounds
    if (x < 0) x = 0;
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;

    return true;
}

GestureType HalTouch::classifyGesture() {
    int16_t dx = lastX_ - startX_;
    int16_t dy = lastY_ - startY_;
    uint32_t duration = millis() - touchStartTime_;
    int16_t absDx = abs(dx);
    int16_t absDy = abs(dy);

    // Check for swipe
    if ((absDx > SWIPE_MIN_DISTANCE || absDy > SWIPE_MIN_DISTANCE) && duration < SWIPE_MAX_TIME) {
        if (absDx > absDy) {
            return dx > 0 ? GestureType::SWIPE_RIGHT : GestureType::SWIPE_LEFT;
        } else {
            return dy > 0 ? GestureType::SWIPE_DOWN : GestureType::SWIPE_UP;
        }
    }

    // Check for long press
    if (duration >= LONG_PRESS_TIME && absDx < LONG_PRESS_MAX_MOVE && absDy < LONG_PRESS_MAX_MOVE) {
        return GestureType::LONG_PRESS;
    }

    // Default to tap
    if (absDx < LONG_PRESS_MAX_MOVE && absDy < LONG_PRESS_MAX_MOVE) {
        return GestureType::TAP;
    }

    return GestureType::NONE;
}

bool HalTouch::poll(TouchEvent& event) {
    uint32_t now = millis();
    if (now - lastPollTime_ < TOUCH_POLL_MS) return false;
    lastPollTime_ = now;

    int16_t x, y;
    bool currentTouch = readRaw(x, y);

    if (currentTouch) {
        lastX_ = x;
        lastY_ = y;
    }

    wasTouching_ = touching_;
    touching_ = currentTouch;

    // Touch just started
    if (touching_ && !wasTouching_) {
        startX_ = x;
        startY_ = y;
        touchStartTime_ = now;
        return false; // Wait for release to classify
    }

    // Touch just released — classify gesture
    if (!touching_ && wasTouching_) {
        event.gesture = classifyGesture();
        event.x = lastX_;
        event.y = lastY_;
        event.startX = startX_;
        event.startY = startY_;
        event.dx = lastX_ - startX_;
        event.dy = lastY_ - startY_;
        event.timestamp = now;
        return event.gesture != GestureType::NONE;
    }

    return false;
}
