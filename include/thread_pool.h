#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <future>
#include <functional>
#include <memory>
#include <atomic>
#include <stdexcept>
#include <exception>
#include <iostream>

#include "blocking_queue.h"
#include "myVector.h"

#define THREAD_POOL_VERSION "V3"
constexpr std::size_t kDefaultQueueCapacity = 1024;

class ThreadPoolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ThreadPoolStopped : public ThreadPoolError {
public:
    ThreadPoolStopped() : ThreadPoolError("ThreadPool has been shutdown, cannot submit new tasks.") {}
};

class ThreadPoolFull : public ThreadPoolError {
public:
    ThreadPoolFull() : ThreadPoolError("ThreadPool task queue is full.") {}
};

class ThreadPool {
public:
    using ExceptionHandler = std::function<void(std::exception_ptr)>;

    explicit ThreadPool(int numThreads, size_t queueCapacity = kDefaultQueueCapacity);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename Func>
    auto submit(Func task) -> std::future<std::invoke_result_t<Func>>;
    template <typename Func>
    auto trySubmit(Func task) -> std::future<std::invoke_result_t<Func>>;
    void setExceptionHandler(ExceptionHandler handler);

    void shutdown();
    void shutdownNow();
    bool isShutdown() const noexcept;

private:
    void workerLoop();
    BlockingQueue<std::function<void()>> taskQueue_;
    myVector<std::thread> workers_;
    std::atomic<bool> stopped_ = false;
    ExceptionHandler exceptionHandler_;
};

inline ThreadPool::ThreadPool(int numThreads, size_t queueCapacity)
     : taskQueue_(queueCapacity)
     , exceptionHandler_([](std::exception_ptr ex) {
        try {
            if (ex) std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            std::cerr << "[UNKNOWN ERROR](default handler)" << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[UNKNOWN ERROR](default handler)" << std::endl;
        }}) {
    workers_.reserve(numThreads);
    for (int i = 0; i < numThreads; i ++) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

inline ThreadPool::~ThreadPool() {
    shutdown();
}

template <typename Func>
inline auto ThreadPool::submit(Func task) -> std::future<std::invoke_result_t<Func>> {
    using ReturnType = std::invoke_result_t<Func>;
    auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(std::move(task));
    std::future<ReturnType> future = packagedTask->get_future();

    QueueStatus result = taskQueue_.push([packagedTask]() { (*packagedTask)(); });
    if (result == QueueStatus::STOPPED) {
        throw ThreadPoolStopped();
    } else if (result != QueueStatus::OK) {
        throw ThreadPoolError("unexpected queue status");
    }
    return future;
}

template <typename Func>
inline auto ThreadPool::trySubmit(Func task) -> std::future<std::invoke_result_t<Func>> {
    using ReturnType = std::invoke_result_t<Func>;
    auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(std::move(task));
    auto future = packagedTask->get_future();

    QueueStatus result = taskQueue_.tryPush([packagedTask]() {(*packagedTask)(); });
    if (result == QueueStatus::STOPPED) {
        throw ThreadPoolStopped();
    } else if (result == QueueStatus::FULL) {
        throw ThreadPoolFull();
    } else if (result != QueueStatus::OK) {
        throw ThreadPoolError("unexpected queue status");
    }
    return future;
}

inline void ThreadPool::setExceptionHandler(ExceptionHandler handler) {
    exceptionHandler_ = std::move(handler);
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

inline void ThreadPool::shutdownNow() {
    if (!stopped_.exchange(true)) {
        taskQueue_.stopAndClear();
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
    while (taskQueue_.pop(task) == QueueStatus::OK) {
        try {
            task();
        } catch (...) {
            try {
                if (exceptionHandler_) {
                    exceptionHandler_(std::current_exception());
                }
            } catch(...) {
                // 避免异常处理器抛出异常导致 worker 线程崩溃
                std::cerr << "[ERROR] Exception in exception handler." << std::endl;
            }
        }
    }
}

#endif // THREAD_POOL_H