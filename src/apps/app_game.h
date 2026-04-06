#pragma once
#include "../core/app_framework.h"

struct Point {
    int8_t x, y;
};

enum class Direction : uint8_t {
    UP, DOWN, LEFT, RIGHT
};

enum class GameState : uint8_t {
    PLAYING,
    GAME_OVER,
    PAUSED
};

class GameApp : public App {
public:
    void onCreate() override;
    void onDestroy() override;
    void onEvent(const Event& event) override;
    void update(uint32_t dt) override;
    void render(TFT_eSprite& canvas) override;
    bool handleBackGesture() override;
    AppId getAppId() const override { return APP_GAME; }

private:
    void resetGame();
    void moveSnake();
    void spawnFood();
    bool checkCollision(int8_t x, int8_t y);

    static constexpr uint8_t GRID_W = 24;
    static constexpr uint8_t GRID_H = 25;
    static constexpr uint8_t CELL = 10;
    static constexpr uint16_t MAX_SNAKE = 200;

    Point snake_[MAX_SNAKE];
    uint16_t snakeLen_ = 3;
    Direction dir_ = Direction::RIGHT;
    Direction nextDir_ = Direction::RIGHT;
    Point food_ = {10, 10};
    GameState state_ = GameState::PLAYING;
    uint16_t score_ = 0;
    uint16_t highScore_ = 0;

    uint32_t moveTimer_ = 0;
    uint16_t moveInterval_ = 200;  // ms per move
};
