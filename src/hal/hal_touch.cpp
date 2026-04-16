#include "hal_touch.h"

HalTouch touch;

#if SOLWEAR_HAS_BUTTONS
void HalTouch::init() {
    pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);
    pinMode(PIN_BUTTON_HASH, INPUT_PULLUP);
    pinMode(PIN_BUTTON_STAR, INPUT_PULLUP);
}

bool HalTouch::readRaw(int16_t& x, int16_t& y) { return false; }
GestureType HalTouch::classifyGesture() { return GestureType::NONE; }

bool HalTouch::poll(TouchEvent& event) {
    uint32_t now = millis();
    if (now - lastPollTime_ < TOUCH_POLL_MS) return false;
    lastPollTime_ = now;

    bool upRaw = (digitalRead(PIN_BUTTON_UP) == LOW);
    bool downRaw = (digitalRead(PIN_BUTTON_DOWN) == LOW);
    bool hashRaw = (digitalRead(PIN_BUTTON_HASH) == LOW);
    bool starRaw = (digitalRead(PIN_BUTTON_STAR) == LOW);

    bool upPressed = upRaw && !lastUp_;
    bool downPressed = downRaw && !lastDown_;
    bool hashPressed = hashRaw && !lastHash_;
    bool starPressed = starRaw && !lastStar_;

    lastUp_ = upRaw;
    lastDown_ = downRaw;
    lastHash_ = hashRaw;
    lastStar_ = starRaw;

    event.x = SCREEN_WIDTH / 2;
    event.y = SCREEN_HEIGHT / 2;
    event.startX = event.x;
    event.startY = event.y;
    event.dx = 0;
    event.dy = 0;
    event.timestamp = now;
    event.gesture = GestureType::NONE;

    if (upPressed) {
        event.gesture = GestureType::SWIPE_DOWN; 
        return true;
    } 
    if (downPressed) {
        event.gesture = GestureType::SWIPE_UP;
        return true;
    }
    if (hashPressed) {
        event.gesture = GestureType::SWIPE_RIGHT; 
        return true;
    }
    if (starPressed) {
        event.gesture = GestureType::TAP; 
        return true;
    }

    return false;
}
#else
// CST816S register addresses
#define CST816S_REG_GESTURE   0x01
#define CST816S_REG_FINGER    0x02
#define CST816S_REG_XH        0x03
#define CST816S_REG_XL        0x04
#define CST816S_REG_YH        0x05
#define CST816S_REG_YL        0x06

void HalTouch::init() {
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(50);
    pinMode(PIN_TOUCH_INT, INPUT);
}

bool HalTouch::readRaw(int16_t& x, int16_t& y) {
#if !SOLWEAR_HAS_TOUCH
    return false;
#endif
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

    if ((absDx > SWIPE_MIN_DISTANCE || absDy > SWIPE_MIN_DISTANCE) && duration < SWIPE_MAX_TIME) {
        if (absDx > absDy) {
            return dx > 0 ? GestureType::SWIPE_RIGHT : GestureType::SWIPE_LEFT;
        } else {
            return dy > 0 ? GestureType::SWIPE_DOWN : GestureType::SWIPE_UP;
        }
    }
    if (duration >= LONG_PRESS_TIME && absDx < LONG_PRESS_MAX_MOVE && absDy < LONG_PRESS_MAX_MOVE) {
        return GestureType::LONG_PRESS;
    }
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

    if (touching_ && !wasTouching_) {
        startX_ = x;
        startY_ = y;
        touchStartTime_ = now;
        return false;
    }

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
#endif
