#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

#define ERROR_QUEUE_STOPPED 1

template <typename T>
class BlockingQueue {
public:
    int push(T item);
    int pop(T& item);
    void stop();

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

template <typename T>
int BlockingQueue<T>::push(T item) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return ERROR_QUEUE_STOPPED;
        queue_.push(std::move(item));
    }
    cv_.notify_one();
    return 0;
}

template <typename T>
int BlockingQueue<T>::pop(T& item) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });
    if (!queue_.empty()) {
        item = std::move(queue_.front());
        queue_.pop();
        return 0;
    }
    return ERROR_QUEUE_STOPPED;
}

template <typename T>
void BlockingQueue<T>::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}

#endif // BLOCKING_QUEUE_H