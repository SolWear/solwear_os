#include "storage.h"
#include <stdio.h>
#include <string.h>

bool Storage::init() {
    mounted_ = LittleFS.begin();
    if (!mounted_) {
        // Try formatting
        LittleFS.format();
        mounted_ = LittleFS.begin();
    }
    if (mounted_) {
        Serial.println("LittleFS mounted");
    } else {
        Serial.println("LittleFS mount failed");
    }
    return mounted_;
}

bool Storage::loadSettings(Settings& s) {
    if (!mounted_) return false;

    File f = LittleFS.open("/settings.json", "r");
    if (!f) return false;

    char buf[256];
    size_t len = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    buf[len] = '\0';

    // Simple JSON parsing with sscanf
    char* p;
    if ((p = strstr(buf, "\"brightness\":"))) sscanf(p + 13, "%hhu", &s.brightness);
    if ((p = strstr(buf, "\"sound\":")))      { int v; sscanf(p + 8, "%d", &v); s.soundEnabled = v; }
    if ((p = strstr(buf, "\"vibration\":")))   { int v; sscanf(p + 12, "%d", &v); s.vibrationEnabled = v; }
    if ((p = strstr(buf, "\"watchFace\":")))   sscanf(p + 12, "%hhu", &s.watchFaceIndex);
    if ((p = strstr(buf, "\"timeFormat24h\":"))) { int v; sscanf(p + 16, "%d", &v); s.timeFormat24h = v; }
    if ((p = strstr(buf, "\"wallpaper\":")))   sscanf(p + 12, "%hhu", &s.wallpaperIndex);
    if ((p = strstr(buf, "\"stepGoal\":")))    sscanf(p + 11, "%hu", &s.stepGoal);
    if ((p = strstr(buf, "\"batteryDivider\":"))) sscanf(p + 17, "%f", &s.batteryDivider);

    return true;
}

bool Storage::saveSettings(const Settings& s) {
    if (!mounted_) return false;

    File f = LittleFS.open("/settings.json", "w");
    if (!f) return false;

    char buf[320];
    snprintf(buf, sizeof(buf),
        "{\n"
        "  \"brightness\": %u,\n"
        "  \"sound\": %d,\n"
        "  \"vibration\": %d,\n"
        "  \"watchFace\": %u,\n"
        "  \"timeFormat24h\": %d,\n"
        "  \"wallpaper\": %u,\n"
        "  \"stepGoal\": %u,\n"
        "  \"batteryDivider\": %.4f\n"
        "}\n",
        s.brightness, s.soundEnabled ? 1 : 0, s.vibrationEnabled ? 1 : 0,
        s.watchFaceIndex, s.timeFormat24h ? 1 : 0, s.wallpaperIndex, s.stepGoal, s.batteryDivider);

    f.print(buf);
    f.close();
    return true;
}

bool Storage::loadWallet(WalletData& w) {
    if (!mounted_) return false;

    File f = LittleFS.open("/wallet.json", "r");
    if (!f) { w.hasAddress = false; return false; }

    char buf[256];
    size_t len = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    buf[len] = '\0';

    char* p = strstr(buf, "\"address\":\"");
    if (p) {
        p += 11;
        char* end = strchr(p, '"');
        if (end) {
            size_t addrLen = end - p;
            if (addrLen > 63) addrLen = 63;
            memcpy(w.address, p, addrLen);
            w.address[addrLen] = '\0';
            w.hasAddress = (addrLen > 0);
        }
    }

    return true;
}

bool Storage::saveWallet(const WalletData& w) {
    if (!mounted_) return false;

    File f = LittleFS.open("/wallet.json", "w");
    if (!f) return false;

    char buf[128];
    snprintf(buf, sizeof(buf), "{\"address\":\"%s\"}", w.address);
    f.print(buf);
    f.close();
    return true;
}

bool Storage::saveSteps(uint8_t dayIndex, uint32_t steps) {
    if (!mounted_) return false;

    // Binary file: 7 x uint32_t (one per weekday, 0=Mon, 6=Sun)
    File f = LittleFS.open("/steps.bin", "r+");
    if (!f) {
        f = LittleFS.open("/steps.bin", "w");
        if (!f) return false;
        uint32_t zeros[7] = {};
        f.write((uint8_t*)zeros, sizeof(zeros));
        f.close();
        f = LittleFS.open("/steps.bin", "r+");
        if (!f) return false;
    }

    f.seek(dayIndex * sizeof(uint32_t));
    f.write((uint8_t*)&steps, sizeof(uint32_t));
    f.close();
    return true;
}

bool Storage::loadSteps(uint8_t dayIndex, uint32_t& steps) {
    if (!mounted_) return false;

    File f = LittleFS.open("/steps.bin", "r");
    if (!f) { steps = 0; return false; }

    f.seek(dayIndex * sizeof(uint32_t));
    if (f.read((uint8_t*)&steps, sizeof(uint32_t)) != sizeof(uint32_t)) {
        steps = 0;
        f.close();
        return false;
    }
    f.close();
    return true;
}

bool Storage::loadWeekSteps(uint32_t weekSteps[7]) {
    if (!mounted_) return false;

    File f = LittleFS.open("/steps.bin", "r");
    if (!f) {
        memset(weekSteps, 0, 7 * sizeof(uint32_t));
        return false;
    }

    if (f.read((uint8_t*)weekSteps, 7 * sizeof(uint32_t)) != 7 * sizeof(uint32_t)) {
        memset(weekSteps, 0, 7 * sizeof(uint32_t));
        f.close();
        return false;
    }
    f.close();
    return true;
}
