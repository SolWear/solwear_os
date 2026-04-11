#pragma once
#include <Arduino.h>

struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t dayOfWeek;  // 0=Sun, 1=Mon, ... 6=Sat
};

class SystemClock {
public:
    static SystemClock& instance() {
        static SystemClock inst;
        return inst;
    }

    void init();
    void update();
    DateTime now() const;
    void setUnixEpoch(uint32_t epoch);
    void setTime(uint16_t year, uint8_t month, uint8_t day,
                 uint8_t hour, uint8_t minute, uint8_t second);

    // Formatted strings
    void formatTime(char* buf, size_t len) const;      // "HH:MM"
    void formatTimeSec(char* buf, size_t len) const;    // "HH:MM:SS"
    void formatDate(char* buf, size_t len) const;       // "Mon Apr 5"
    void formatDateShort(char* buf, size_t len) const;  // "04/05"

    uint8_t getDayOfWeekIndex() const;  // 0=Mon ... 6=Sun (for step history)

private:
    SystemClock() {}

    uint32_t lastMillis_ = 0;
    uint32_t carryMs_ = 0;
    uint32_t unixEpoch_ = 1743868800; // April 5, 2026 00:00:00 UTC (default)

    static const char* dayNames_[7];
    static const char* monthNames_[12];

    uint8_t calcDayOfWeek(uint16_t y, uint8_t m, uint8_t d) const;
};
