#include "event_system.h"

bool EventSystem::post(const Event& event) {
    uint8_t nextHead = (head_ + 1) % EVENT_QUEUE_SIZE;
    if (nextHead == tail_) {
        return false;  // Queue full
    }
    queue_[head_] = event;
    head_ = nextHead;
    return true;
}

bool EventSystem::poll(Event& event) {
    if (head_ == tail_) {
        return false;  // Queue empty
    }
    event = queue_[tail_];
    tail_ = (tail_ + 1) % EVENT_QUEUE_SIZE;
    return true;
}
