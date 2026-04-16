#include "app_nfc.h"
#include "../core/screen_manager.h"
#include "../core/storage.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"
#include "../hal/hal_buzzer.h"
#include "../assets/sounds.h"
#include <math.h>

void NfcApp::onCreate() {
    statusBar.setTitle("NFC");
    // Power up the PN532 only when the user opens the NFC app.
    if (!nfc.ensureInit()) {
        Serial.println("[APP] NFC: hardware not available");
    }
}

void NfcApp::onDestroy() {
    statusBar.clearTitle();
    // Power down PN532 when leaving the app — it must be off by default.
    nfc.shutdown();
}

void NfcApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

#if SOLWEAR_HAS_BUTTONS
    switch (mode_) {
        case NfcMode::IDLE:
            if (event.touch.gesture == GestureType::SWIPE_DOWN) { // K1 UP Button (Wait, menu goes up? No, K1 maps to SWIPE_DOWN gesture, which means 'scroll down', moving cursor UP visually)
                selectedIndex_ = (selectedIndex_ <= 0) ? 2 : selectedIndex_ - 1;
            } else if (event.touch.gesture == GestureType::SWIPE_UP) { // K2 DOWN Button
                selectedIndex_ = (selectedIndex_ + 1) % 3;
            } else if (event.touch.gesture == GestureType::TAP) { // K4 STAR
                if (selectedIndex_ == 0) { mode_ = NfcMode::SCANNING; }
                else if (selectedIndex_ == 1) { mode_ = NfcMode::WRITING; }
                else { mode_ = NfcMode::CONTRACT_TEST; }
                buzzer.click();
            } else if (event.touch.gesture == GestureType::SWIPE_RIGHT) { // K3 HASH
                ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
            }
            break;
            
        default:
            if (event.touch.gesture == GestureType::TAP || event.touch.gesture == GestureType::SWIPE_RIGHT) {
                mode_ = NfcMode::IDLE;
            }
            break;
    }
#else
    switch (mode_) {
        case NfcMode::IDLE:
            if (event.touch.gesture == GestureType::TAP) {
                int16_t ty = event.touch.y;
                if (ty < 140) {
                    // "Scan Tag" button area
                    mode_ = NfcMode::SCANNING;
                    buzzer.click();
                } else if (ty < 200) {
                    // "Write Address" button area
                    mode_ = NfcMode::WRITING;
                    buzzer.click();
                } else {
                    // "Contract Test" button area
                    mode_ = NfcMode::CONTRACT_TEST;
                    buzzer.click();
                }
            } else if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
                ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
            }
            break;

        case NfcMode::TAG_FOUND:
        case NfcMode::WRITE_OK:
        case NfcMode::WRITE_FAIL:
        case NfcMode::CONTRACT_TEST:
            if (event.touch.gesture == GestureType::TAP || event.touch.gesture == GestureType::SWIPE_RIGHT) {
                mode_ = NfcMode::IDLE;
            }
            break;

        case NfcMode::SCANNING:
        case NfcMode::WRITING:
            if (event.touch.gesture == GestureType::TAP || event.touch.gesture == GestureType::SWIPE_RIGHT) {
                mode_ = NfcMode::IDLE;  // Cancel
            }
            break;
    }
#endif
}

void NfcApp::update(uint32_t dt) {
    animTimer_ += dt;
    if (animTimer_ >= 300) {
        animTimer_ = 0;
        animFrame_ = (animFrame_ + 1) % 4;
    }

    if (mode_ == NfcMode::SCANNING) {
        NfcTagInfo tag;
        if (nfc.waitForTag(NFC_SCAN_TIMEOUT_MS, tag)) {
            tagInfo_ = tag;
            // Try reading NDEF
            nfc.readNdef(ndefData_, sizeof(ndefData_));
            mode_ = NfcMode::TAG_FOUND;
            buzzer.playMelody(Sounds::NFC_FOUND, Sounds::NFC_FOUND_LEN);
        }
    }

    if (mode_ == NfcMode::WRITING) {
        NfcTagInfo tag;
        if (nfc.waitForTag(NFC_SCAN_TIMEOUT_MS, tag)) {
            WalletData wallet;
            Storage::instance().loadWallet(wallet);
            char uri[80];
            snprintf(uri, sizeof(uri), "solana:%s", wallet.address);

            if (nfc.writeNdefUrl(uri)) {
                mode_ = NfcMode::WRITE_OK;
                buzzer.playMelody(Sounds::SUCCESS, Sounds::SUCCESS_LEN);
            } else {
                mode_ = NfcMode::WRITE_FAIL;
                buzzer.playMelody(Sounds::ERROR, Sounds::ERROR_LEN);
            }
            resultTime_ = millis();
        }
    }
}

void NfcApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);
    statusBar.render(canvas);

    switch (mode_) {
        case NfcMode::IDLE:          renderIdle(canvas); break;
        case NfcMode::SCANNING:      renderScanning(canvas); break;
        case NfcMode::TAG_FOUND:     renderTagFound(canvas); break;
        case NfcMode::WRITING:       renderWriting(canvas); break;
        case NfcMode::WRITE_OK:      renderResult(canvas, true); break;
        case NfcMode::WRITE_FAIL:    renderResult(canvas, false); break;
        case NfcMode::CONTRACT_TEST: renderContractTest(canvas); break;
    }
}

void NfcApp::renderIdle(TFT_eSprite& canvas) {
    // NFC icon
    int16_t cx = SCREEN_WIDTH / 2;
    canvas.drawCircle(cx, 70, 8, Theme::ACCENT);
    canvas.drawCircle(cx, 70, 16, Theme::ACCENT);
    canvas.drawCircle(cx, 70, 24, Theme::ACCENT);

#if SOLWEAR_HAS_BUTTONS
    // Scan button
    Draw::roundRect(canvas, 30, 110, 180, 40, Theme::CORNER_RADIUS, selectedIndex_ == 0 ? TFT_WHITE : Theme::ACCENT);
    Draw::drawCenteredText(canvas, "Scan Tag", 120, 2, selectedIndex_ == 0 ? TFT_BLACK : Theme::BG_PRIMARY);

    // Write button
    Draw::roundRect(canvas, 30, 165, 180, 40, Theme::CORNER_RADIUS, selectedIndex_ == 1 ? TFT_WHITE : Theme::ACCENT_SOL);
    Draw::drawCenteredText(canvas, "Write Address", 175, 2, selectedIndex_ == 1 ? TFT_BLACK : Theme::TEXT_PRIMARY);

    // Contract test button
    Draw::roundRect(canvas, 30, 220, 180, 40, Theme::CORNER_RADIUS, selectedIndex_ == 2 ? TFT_WHITE : Theme::BG_CARD);
    Draw::drawCenteredText(canvas, "Contract Test", 230, 2, selectedIndex_ == 2 ? TFT_BLACK : Theme::ACCENT_GREEN);
#else
    // Scan button
    Draw::roundRect(canvas, 30, 110, 180, 40, Theme::CORNER_RADIUS, Theme::ACCENT);
    Draw::drawCenteredText(canvas, "Scan Tag", 120, 2, Theme::BG_PRIMARY);

    // Write button
    Draw::roundRect(canvas, 30, 165, 180, 40, Theme::CORNER_RADIUS, Theme::ACCENT_SOL);
    Draw::drawCenteredText(canvas, "Write Address", 175, 2, Theme::TEXT_PRIMARY);

    // Contract test button
    Draw::roundRect(canvas, 30, 220, 180, 40, Theme::CORNER_RADIUS, Theme::BG_CARD);
    Draw::drawCenteredText(canvas, "Contract Test", 230, 2, Theme::ACCENT_GREEN);
#endif
}

void NfcApp::renderScanning(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Scanning...", APP_AREA_Y + 10, 2, Theme::TEXT_PRIMARY);

    // Animated NFC rings
    int16_t cx = SCREEN_WIDTH / 2;
    int16_t cy = 140;
    for (int i = 0; i < 4; i++) {
        int16_t r = 15 + i * 15;
        uint8_t alpha = (i == animFrame_) ? 255 : 80;
        uint16_t color = canvas.color565(0, alpha, alpha);
        canvas.drawCircle(cx, cy, r, color);
    }

    Draw::drawCenteredText(canvas, "Hold tag near device", 220, 1, Theme::TEXT_MUTED);
    Draw::drawCenteredText(canvas, "Tap to cancel", 240, 1, Theme::TEXT_MUTED);
}

void NfcApp::renderTagFound(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Tag Found!", APP_AREA_Y + 10, 2, Theme::SUCCESS);

    // UID
    char uidStr[24] = "";
    for (uint8_t i = 0; i < tagInfo_.uidLength; i++) {
        char hex[4];
        snprintf(hex, sizeof(hex), "%02X ", tagInfo_.uid[i]);
        strcat(uidStr, hex);
    }
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString("UID:", Theme::PADDING, 80, 2);
    canvas.setTextColor(Theme::ACCENT);
    canvas.drawString(uidStr, Theme::PADDING, 100, 1);

    // NDEF data if found
    if (strlen(ndefData_) > 0) {
        canvas.setTextColor(Theme::TEXT_PRIMARY);
        canvas.drawString("NDEF:", Theme::PADDING, 130, 2);
        canvas.setTextColor(Theme::ACCENT_GREEN);
        // Wrap long text
        int16_t y = 150;
        size_t len = strlen(ndefData_);
        for (size_t i = 0; i < len; i += 28) {
            char line[30];
            strncpy(line, ndefData_ + i, 28);
            line[28] = '\0';
            canvas.drawString(line, Theme::PADDING, y, 1);
            y += 14;
        }
    }

    Draw::drawCenteredText(canvas, "Tap to dismiss", SCREEN_HEIGHT - 20, 1, Theme::TEXT_MUTED);
}

void NfcApp::renderWriting(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Writing...", APP_AREA_Y + 10, 2, Theme::ACCENT_SOL);

    // Animated dots
    int16_t cx = SCREEN_WIDTH / 2;
    for (int i = 0; i < 3; i++) {
        uint16_t color = (i <= (int)animFrame_ % 3) ? Theme::ACCENT_SOL : Theme::BG_CARD;
        canvas.fillCircle(cx - 20 + i * 20, 140, 5, color);
    }

    Draw::drawCenteredText(canvas, "Hold tag near device", 190, 1, Theme::TEXT_MUTED);
    Draw::drawCenteredText(canvas, "Writing Solana address", 210, 1, Theme::TEXT_MUTED);
}

void NfcApp::renderResult(TFT_eSprite& canvas, bool success) {
    if (success) {
        canvas.fillCircle(SCREEN_WIDTH / 2, 110, 30, Theme::SUCCESS);
        // Checkmark
        canvas.drawLine(105, 110, 117, 122, Theme::BG_PRIMARY);
        canvas.drawLine(117, 122, 135, 98, Theme::BG_PRIMARY);
        Draw::drawCenteredText(canvas, "Success!", 160, 2, Theme::SUCCESS);
        Draw::drawCenteredText(canvas, "Address written to tag", 190, 1, Theme::TEXT_SECONDARY);
    } else {
        canvas.fillCircle(SCREEN_WIDTH / 2, 110, 30, Theme::DANGER);
        // X mark
        canvas.drawLine(108, 98, 132, 122, Theme::BG_PRIMARY);
        canvas.drawLine(132, 98, 108, 122, Theme::BG_PRIMARY);
        Draw::drawCenteredText(canvas, "Failed!", 160, 2, Theme::DANGER);
        Draw::drawCenteredText(canvas, "Could not write to tag", 190, 1, Theme::TEXT_SECONDARY);
    }

    Draw::drawCenteredText(canvas, "Tap to continue", SCREEN_HEIGHT - 20, 1, Theme::TEXT_MUTED);
}

void NfcApp::renderContractTest(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Solana Contract Test", APP_AREA_Y + 10, 2, Theme::ACCENT_GREEN);

    int16_t y = APP_AREA_Y + 40;
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString("Test Transaction:", Theme::PADDING, y, 2); y += 24;

    canvas.setTextColor(Theme::TEXT_SECONDARY);
    canvas.drawString("Program: System", Theme::PADDING, y, 1); y += 16;
    canvas.drawString("Instruction: Transfer", Theme::PADDING, y, 1); y += 16;
    canvas.drawString("Amount: 0.001 SOL", Theme::PADDING, y, 1); y += 24;

    // Status
    canvas.setTextColor(Theme::WARNING);
    canvas.drawString("Status: Simulated", Theme::PADDING, y, 2); y += 24;

    canvas.setTextColor(Theme::TEXT_MUTED);
    canvas.drawString("This is a test stub.", Theme::PADDING, y, 1); y += 14;
    canvas.drawString("Real signing requires", Theme::PADDING, y, 1); y += 14;
    canvas.drawString("companion app + BLE.", Theme::PADDING, y, 1);

    // Mock transaction hash
    Draw::roundRect(canvas, Theme::PADDING, 230, SCREEN_WIDTH - Theme::PADDING * 2, 28,
                    Theme::SMALL_RADIUS, Theme::BG_CARD);
    canvas.setTextColor(Theme::ACCENT);
    canvas.drawString("TX: 5xKm...9pQr (mock)", Theme::PADDING + 6, 236, 1);

    Draw::drawCenteredText(canvas, "Tap to go back", SCREEN_HEIGHT - 12, 1, Theme::TEXT_MUTED);
}
