#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <future>
#include <functional>
#include <memory>
#include <atomic>
#include <stdexcept>

#include "blocking_queue.h"
#include "myVector.h"

class ThreadPool {
public:
    explicit ThreadPool(int numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename Func>
    auto submit(Func task) -> std::future<std::invoke_result_t<Func>>;

    void shutdown();
    bool isShutdown() const noexcept;

private:
    void workerLoop();
    BlockingQueue<std::function<void()>> taskQueue_;
    myVector<std::thread> workers_;
    std::atomic<bool> stopped_ = false;
};

inline ThreadPool::ThreadPool(int numThreads) {
    workers_.reserve(numThreads);
    for (int i = 0; i < numThreads; i ++) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

inline ThreadPool::~ThreadPool() {
    shutdown();
}

template <typename Func>
auto ThreadPool::submit(Func task) -> std::future<std::invoke_result_t<Func>> {
    using ReturnType = std::invoke_result_t<Func>;
    auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(std::move(task));
    std::future<ReturnType> future = packagedTask->get_future();
    int result = taskQueue_.push([packagedTask]() { (*packagedTask)(); });
    if (result == ERROR_QUEUE_STOPPED) {
        throw std::runtime_error("[ERROR_QUEUE_STOPPED]ThreadPool has been shutdown, cannot submit new tasks.");
    } else if (result != 0) {
        throw std::runtime_error("[ERROR_UNKNOWN]Failed to submit task to the ThreadPool.");
    }
    return future;
}

inline void ThreadPool::shutdown() {
    if (!stopped_.exchange(true)) {
        taskQueue_.stop();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

inline bool ThreadPool::isShutdown() const noexcept {
    return stopped_.load();
}

inline void ThreadPool::workerLoop() {
    std::function<void()> task;
    while (taskQueue_.pop(task) == 0) {
        task();
    }
}

#endif // THREAD_POOL_H