#include "app_wallet.h"
#include "../core/screen_manager.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"
#include "../ui/qr_display.h"

// Default demo Solana address
#define DEFAULT_SOL_ADDRESS "5Gh7mKxEPzs7VhC35XjZxjLfTDGPyFqoNiPdxFbJPYRm"

void WalletApp::onCreate() {
    if (!Storage::instance().loadWallet(wallet_) || !wallet_.hasAddress) {
        strncpy(wallet_.address, DEFAULT_SOL_ADDRESS, sizeof(wallet_.address));
        wallet_.hasAddress = true;
    }
    statusBar.setTitle("Wallet");
}

void WalletApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

#if SOLWEAR_HAS_BUTTONS
    if (event.touch.gesture == GestureType::SWIPE_DOWN) {
        if (view_ > 0) view_--;
    } else if (event.touch.gesture == GestureType::SWIPE_UP) {
        if (view_ < 2) view_++;
    } else if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
    }
#else
    if (event.touch.gesture == GestureType::SWIPE_LEFT) {
        if (view_ < 2) view_++;
    } else if (event.touch.gesture == GestureType::SWIPE_RIGHT) {
        if (view_ > 0) view_--;
        else ScreenManager::instance().popScreen(Transition::SLIDE_RIGHT);
    }
#endif
}

void WalletApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);
    statusBar.render(canvas);

    switch (view_) {
        case 0: renderAddress(canvas); break;
        case 1: renderBalance(canvas); break;
        case 2: renderHistory(canvas); break;
    }

    // View indicator dots
    for (int i = 0; i < 3; i++) {
        int16_t dotX = SCREEN_WIDTH / 2 - 12 + i * 12;
        if (i == view_) {
            canvas.fillCircle(dotX, SCREEN_HEIGHT - 10, 3, Theme::ACCENT_SOL);
        } else {
            canvas.drawCircle(dotX, SCREEN_HEIGHT - 10, 3, Theme::TEXT_MUTED);
        }
    }
}

void WalletApp::renderAddress(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Your Address", APP_AREA_Y + 8, 2, Theme::TEXT_SECONDARY);

    // QR code
    if (wallet_.hasAddress) {
        // Create solana: URI for QR
        char uri[80];
        snprintf(uri, sizeof(uri), "solana:%s", wallet_.address);
        QrDisplay::render(canvas, uri, SCREEN_WIDTH / 2, 140, 180,
                         Theme::TEXT_PRIMARY, TFT_WHITE);
    }

    // Truncated address below QR
    if (wallet_.hasAddress) {
        char truncated[20];
        size_t len = strlen(wallet_.address);
        if (len > 12) {
            snprintf(truncated, sizeof(truncated), "%.4s...%.4s",
                     wallet_.address, wallet_.address + len - 4);
        } else {
            strncpy(truncated, wallet_.address, sizeof(truncated));
        }
        Draw::drawCenteredText(canvas, truncated, 240, 2, Theme::ACCENT_SOL);
    }
}

void WalletApp::renderBalance(TFT_eSprite& canvas) {
    // Solana logo area
    canvas.fillCircle(SCREEN_WIDTH / 2, APP_AREA_Y + 50, 25, Theme::ACCENT_SOL);
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    Draw::drawCenteredText(canvas, "SOL", APP_AREA_Y + 42, 2, Theme::TEXT_PRIMARY);

    // Balance (placeholder — no network connection)
    Draw::drawCenteredText(canvas, "-- SOL", 140, 4, Theme::TEXT_PRIMARY);

    // USD equivalent
    Draw::drawCenteredText(canvas, "$--.--", 180, 2, Theme::TEXT_SECONDARY);

    // Note
    Draw::drawCenteredText(canvas, "Connect via NFC", 220, 1, Theme::TEXT_MUTED);
    Draw::drawCenteredText(canvas, "to sync balance", 232, 1, Theme::TEXT_MUTED);
}

void WalletApp::renderHistory(TFT_eSprite& canvas) {
    Draw::drawCenteredText(canvas, "Transactions", APP_AREA_Y + 8, 2, Theme::TEXT_SECONDARY);

    // Empty state
    canvas.drawCircle(SCREEN_WIDTH / 2, 130, 30, Theme::BG_CARD);
    Draw::drawCenteredText(canvas, "~", 122, 4, Theme::TEXT_MUTED);
    Draw::drawCenteredText(canvas, "No transactions yet", 175, 2, Theme::TEXT_MUTED);
    Draw::drawCenteredText(canvas, "Use NFC to send/receive", 200, 1, Theme::TEXT_MUTED);
}
