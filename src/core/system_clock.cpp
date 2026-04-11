#include "system_clock.h"
#include <stdio.h>

const char* SystemClock::dayNames_[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* SystemClock::monthNames_[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

void SystemClock::init() {
    lastMillis_ = millis();
    carryMs_ = 0;
}

void SystemClock::update() {
    uint32_t now = millis();
    uint32_t elapsed = now - lastMillis_;
    lastMillis_ = now;
    carryMs_ += elapsed;
    if (carryMs_ >= 1000UL) {
        uint32_t deltaSec = carryMs_ / 1000UL;
        unixEpoch_ += deltaSec;
        carryMs_ -= (deltaSec * 1000UL);
    }
}

void SystemClock::setUnixEpoch(uint32_t epoch) {
    unixEpoch_ = epoch;
    lastMillis_ = millis();
    carryMs_ = 0;
}

void SystemClock::setTime(uint16_t year, uint8_t month, uint8_t day,
                           uint8_t hour, uint8_t minute, uint8_t second) {
    // Convert to approximate Unix epoch
    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; y++) {
        days += (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    }
    static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    for (uint8_t m = 1; m < month; m++) {
        days += daysInMonth[m - 1];
        if (m == 2 && leap) days++;
    }
    days += day - 1;

    unixEpoch_ = days * 86400UL + hour * 3600UL + minute * 60UL + second;
    lastMillis_ = millis();
    carryMs_ = 0;
}

DateTime SystemClock::now() const {
    DateTime dt;
    uint32_t epoch = unixEpoch_;

    dt.second = epoch % 60; epoch /= 60;
    dt.minute = epoch % 60; epoch /= 60;
    dt.hour = epoch % 24;   epoch /= 24;

    // Days since 1970-01-01
    uint32_t days = epoch;
    dt.dayOfWeek = (days + 4) % 7; // 1970-01-01 was Thursday (4)

    uint16_t year = 1970;
    while (true) {
        uint16_t daysInYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (days < daysInYear) break;
        days -= daysInYear;
        year++;
    }
    dt.year = year;

    static const uint8_t daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));

    uint8_t month = 0;
    while (month < 12) {
        uint8_t dim = daysInMonth[month];
        if (month == 1 && leap) dim++;
        if (days < dim) break;
        days -= dim;
        month++;
    }
    dt.month = month + 1;
    dt.day = days + 1;

    return dt;
}

void SystemClock::formatTime(char* buf, size_t len) const {
    DateTime dt = now();
    snprintf(buf, len, "%02d:%02d", dt.hour, dt.minute);
}

void SystemClock::formatTimeSec(char* buf, size_t len) const {
    DateTime dt = now();
    snprintf(buf, len, "%02d:%02d:%02d", dt.hour, dt.minute, dt.second);
}

void SystemClock::formatDate(char* buf, size_t len) const {
    DateTime dt = now();
    snprintf(buf, len, "%s %s %d", dayNames_[dt.dayOfWeek], monthNames_[dt.month - 1], dt.day);
}

void SystemClock::formatDateShort(char* buf, size_t len) const {
    DateTime dt = now();
    snprintf(buf, len, "%02d/%02d", dt.month, dt.day);
}

uint8_t SystemClock::getDayOfWeekIndex() const {
    DateTime dt = now();
    // Convert Sun=0..Sat=6 to Mon=0..Sun=6
    return (dt.dayOfWeek + 6) % 7;
}

uint8_t SystemClock::calcDayOfWeek(uint16_t y, uint8_t m, uint8_t d) const {
    // Zeller's algorithm (simplified)
    if (m < 3) { m += 12; y--; }
    return (d + 13 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400) % 7;
}
