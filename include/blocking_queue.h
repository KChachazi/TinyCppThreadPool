#ifndef BLOCKING_QUEUE_H
#define BLOCKING_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstddef>

enum class QueueStatus : int {
    OK = 0,
    STOPPED = 1,
    FULL = 2,
};

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity = 0) : capacity_(capacity) {}
    
    QueueStatus push(T item);
    QueueStatus tryPush(T item);
    QueueStatus pop(T& item);

    void stop();
    void stopAndClear();
    void setCapacity(std::size_t capacity);

private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::size_t capacity_;
    bool stopped_ = false;
};

template <typename T>
QueueStatus BlockingQueue<T>::push(T item) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this] {
            return stopped_ || capacity_ == 0 || queue_.size() < capacity_;
        });
        if (stopped_) {
            return QueueStatus::STOPPED;
        }
        queue_.push(std::move(item));
    }
    notEmpty_.notify_one();
    return QueueStatus::OK;
}

template <typename T>
QueueStatus BlockingQueue<T>::tryPush(T item) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            return QueueStatus::STOPPED;
        }
        if (capacity_ > 0 && queue_.size() >= capacity_) {
            return QueueStatus::FULL;
        }
        queue_.push(std::move(item));
    }
    notEmpty_.notify_one();
    return QueueStatus::OK;
}

template <typename T>
QueueStatus BlockingQueue<T>::pop(T& item) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] {
            return stopped_ || !queue_.empty();
        });
        if (!queue_.empty()) {
            item = std::move(queue_.front());
            queue_.pop();
        } else {
            return QueueStatus::STOPPED;
        }
    }
    notFull_.notify_one();
    return QueueStatus::OK;
}

template <typename T>
void BlockingQueue<T>::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
}

template <typename T>
void BlockingQueue<T>::stopAndClear() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T>{}.swap(queue_);
        stopped_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
}

template <typename T>
void BlockingQueue<T>::setCapacity(std::size_t capacity) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = capacity;
    }
    notFull_.notify_all();
}

#endif // BLOCKING_QUEUE_H