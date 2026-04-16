#include "app_game.h"
#include "../core/screen_manager.h"
#include "../ui/ui_common.h"
#include "../ui/status_bar.h"
#include "../hal/hal_buzzer.h"
#include "../assets/sounds.h"

void GameApp::onCreate() {
    statusBar.setTitle("Snake");
    resetGame();
}

void GameApp::onDestroy() {
    statusBar.clearTitle();
}

void GameApp::resetGame() {
    snakeLen_ = 3;
    dir_ = Direction::RIGHT;
    nextDir_ = Direction::RIGHT;
    score_ = 0;
    moveInterval_ = 200;
    moveTimer_ = 0;
    state_ = GameState::PLAYING;

    // Initial snake position
    snake_[0] = {12, 12};
    snake_[1] = {11, 12};
    snake_[2] = {10, 12};

    spawnFood();
}

bool GameApp::handleBackGesture() {
    if (state_ == GameState::PLAYING) {
        state_ = GameState::PAUSED;
        return true;  // Consume gesture — don't exit
    }
    return false;  // Let screen manager handle back
}

void GameApp::onEvent(const Event& event) {
    if (event.type != EventType::TOUCH) return;

    if (state_ == GameState::GAME_OVER) {
        if (event.touch.gesture == GestureType::TAP) {
            resetGame();
        }
        return;
    }

    if (state_ == GameState::PAUSED) {
        if (event.touch.gesture == GestureType::TAP) {
            state_ = GameState::PLAYING;
        }
        return;
    }

    // Direction controls
    // Buttons: K1=SWIPE_DOWN, K2=SWIPE_UP, K3=SWIPE_RIGHT (back/pause), K4=TAP=turn left
    switch (event.touch.gesture) {
        case GestureType::SWIPE_UP:
            if (dir_ != Direction::DOWN) nextDir_ = Direction::UP;
            break;
        case GestureType::SWIPE_DOWN:
            if (dir_ != Direction::UP) nextDir_ = Direction::DOWN;
            break;
        case GestureType::SWIPE_LEFT:
            if (dir_ != Direction::RIGHT) nextDir_ = Direction::LEFT;
            break;
        case GestureType::SWIPE_RIGHT:
            if (dir_ != Direction::LEFT) nextDir_ = Direction::RIGHT;
            break;
#if SOLWEAR_HAS_BUTTONS
        case GestureType::TAP:
            if (dir_ != Direction::RIGHT) nextDir_ = Direction::LEFT;
            break;
#endif
        default:
            break;
    }
}

void GameApp::update(uint32_t dt) {
    if (state_ != GameState::PLAYING) return;

    moveTimer_ += dt;
    if (moveTimer_ >= moveInterval_) {
        moveTimer_ = 0;
        moveSnake();
    }
}

void GameApp::moveSnake() {
    dir_ = nextDir_;

    Point newHead = snake_[0];
    switch (dir_) {
        case Direction::UP:    newHead.y--; break;
        case Direction::DOWN:  newHead.y++; break;
        case Direction::LEFT:  newHead.x--; break;
        case Direction::RIGHT: newHead.x++; break;
    }

    // Wrap around
    if (newHead.x < 0) newHead.x = GRID_W - 1;
    if (newHead.x >= GRID_W) newHead.x = 0;
    if (newHead.y < 0) newHead.y = GRID_H - 1;
    if (newHead.y >= GRID_H) newHead.y = 0;

    // Check self-collision
    if (checkCollision(newHead.x, newHead.y)) {
        state_ = GameState::GAME_OVER;
        if (score_ > highScore_) highScore_ = score_;
        buzzer.playMelody(Sounds::GAME_OVER, Sounds::GAME_OVER_LEN);
        return;
    }

    // Check food
    bool ate = (newHead.x == food_.x && newHead.y == food_.y);

    // Shift body
    if (!ate) {
        // Remove tail
        for (uint16_t i = snakeLen_ - 1; i > 0; i--) {
            snake_[i] = snake_[i - 1];
        }
    } else {
        // Grow: shift everything, don't remove tail
        if (snakeLen_ < MAX_SNAKE) {
            for (uint16_t i = snakeLen_; i > 0; i--) {
                snake_[i] = snake_[i - 1];
            }
            snakeLen_++;
        }
        score_ += 10;
        buzzer.click();
        spawnFood();

        // Speed up
        if (moveInterval_ > 80) moveInterval_ -= 5;
    }

    snake_[0] = newHead;
}

void GameApp::spawnFood() {
    // Find empty cell
    bool valid;
    do {
        food_.x = random(0, GRID_W);
        food_.y = random(0, GRID_H);
        valid = true;
        for (uint16_t i = 0; i < snakeLen_; i++) {
            if (snake_[i].x == food_.x && snake_[i].y == food_.y) {
                valid = false;
                break;
            }
        }
    } while (!valid);
}

bool GameApp::checkCollision(int8_t x, int8_t y) {
    for (uint16_t i = 0; i < snakeLen_; i++) {
        if (snake_[i].x == x && snake_[i].y == y) return true;
    }
    return false;
}

void GameApp::render(TFT_eSprite& canvas) {
    canvas.fillSprite(Theme::BG_PRIMARY);

    // Game area offset (centered under status bar)
    int16_t offsetY = APP_AREA_Y + 2;

    // Grid background
    canvas.fillRect(0, offsetY, GRID_W * CELL, GRID_H * CELL, 0x0841);

    // Draw grid lines (subtle)
    for (int x = 0; x <= GRID_W; x++) {
        canvas.drawFastVLine(x * CELL, offsetY, GRID_H * CELL, 0x1082);
    }
    for (int y = 0; y <= GRID_H; y++) {
        canvas.drawFastHLine(0, offsetY + y * CELL, GRID_W * CELL, 0x1082);
    }

    // Food
    canvas.fillRect(food_.x * CELL + 1, offsetY + food_.y * CELL + 1,
                    CELL - 2, CELL - 2, Theme::DANGER);

    // Snake
    for (uint16_t i = 0; i < snakeLen_; i++) {
        uint16_t color = (i == 0) ? Theme::ACCENT_GREEN : Theme::SUCCESS;
        canvas.fillRect(snake_[i].x * CELL + 1, offsetY + snake_[i].y * CELL + 1,
                       CELL - 2, CELL - 2, color);
    }

    // Score (top bar)
    char scoreBuf[16];
    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %u", score_);
    canvas.setTextColor(Theme::TEXT_PRIMARY);
    canvas.drawString(scoreBuf, 4, 4, 2);

    char hiBuf[16];
    snprintf(hiBuf, sizeof(hiBuf), "Hi: %u", highScore_);
    int16_t tw = canvas.textWidth(hiBuf, 1);
    canvas.setTextColor(Theme::TEXT_MUTED);
    canvas.drawString(hiBuf, SCREEN_WIDTH - tw - 4, 8, 1);

    // Game over overlay
    if (state_ == GameState::GAME_OVER) {
        // Dim overlay
        for (int y = 80; y < 200; y++) {
            for (int x = 0; x < SCREEN_WIDTH; x += 2) {
                canvas.drawPixel(x + (y % 2), y, TFT_BLACK);
            }
        }
        Draw::roundRect(canvas, 30, 100, 180, 80, Theme::CORNER_RADIUS, Theme::BG_CARD);
        Draw::drawCenteredText(canvas, "Game Over!", 110, 2, Theme::DANGER);
        snprintf(scoreBuf, sizeof(scoreBuf), "Score: %u", score_);
        Draw::drawCenteredText(canvas, scoreBuf, 135, 2, Theme::TEXT_PRIMARY);
        Draw::drawCenteredText(canvas, "Tap to restart", 160, 1, Theme::TEXT_MUTED);
    }

    // Paused overlay
    if (state_ == GameState::PAUSED) {
        Draw::roundRect(canvas, 50, 120, 140, 50, Theme::CORNER_RADIUS, Theme::BG_CARD);
        Draw::drawCenteredText(canvas, "PAUSED", 130, 2, Theme::ACCENT);
        Draw::drawCenteredText(canvas, "Tap to resume", 150, 1, Theme::TEXT_MUTED);
    }
}
