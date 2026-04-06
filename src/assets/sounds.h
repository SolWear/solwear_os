#pragma once
#include "../hal/hal_buzzer.h"

// Pre-defined sound effects
// Each melody is an array of Note {frequency, duration_ms}
// frequency=0 means silence (rest)

namespace Sounds {
    // UI click feedback
    static const Note CLICK[] = {{4000, 10}};
    constexpr uint8_t CLICK_LEN = 1;

    // Notification beep
    static const Note BEEP[] = {{2000, 100}, {0, 50}, {2000, 100}};
    constexpr uint8_t BEEP_LEN = 3;

    // Success chime (ascending)
    static const Note SUCCESS[] = {{1000, 80}, {1500, 80}, {2000, 120}};
    constexpr uint8_t SUCCESS_LEN = 3;

    // Error tone (descending)
    static const Note ERROR[] = {{500, 150}, {300, 200}};
    constexpr uint8_t ERROR_LEN = 2;

    // Alarm ring (repeatable)
    static const Note ALARM[] = {
        {1000, 200}, {1500, 200}, {1000, 200}, {1500, 200}, {0, 400}
    };
    constexpr uint8_t ALARM_LEN = 5;

    // Step goal reached celebration
    static const Note GOAL[] = {
        {1000, 100}, {1200, 100}, {1500, 100}, {2000, 200}
    };
    constexpr uint8_t GOAL_LEN = 4;

    // NFC tag detected
    static const Note NFC_FOUND[] = {{1500, 50}, {0, 30}, {2000, 80}};
    constexpr uint8_t NFC_FOUND_LEN = 3;

    // Game over
    static const Note GAME_OVER[] = {{800, 200}, {600, 200}, {400, 300}};
    constexpr uint8_t GAME_OVER_LEN = 3;

    // Boot sound
    static const Note BOOT[] = {
        {800, 80}, {1000, 80}, {1200, 80}, {1600, 150}
    };
    constexpr uint8_t BOOT_LEN = 4;
}
