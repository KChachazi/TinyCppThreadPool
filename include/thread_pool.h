#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <future>
#include <functional>
#include <memory>

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

private:
    void workerLoop();
    BlockingQueue<std::function<void()>> taskQueue_;
    myVector<std::thread> workers_;
};

inline ThreadPool::ThreadPool(int numThreads) {
    workers_.reserve(numThreads);
    for (int i = 0; i < numThreads; i ++) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

inline ThreadPool::~ThreadPool() {
    taskQueue_.stop();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

template <typename Func>
auto ThreadPool::submit(Func task) -> std::future<std::invoke_result_t<Func>> {
    using ReturnType = std::invoke_result_t<Func>;
    auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(std::move(task));
    std::future<ReturnType> future = packagedTask->get_future();
    taskQueue_.push([packagedTask]() { (*packagedTask)(); });
    return future;
}

inline void ThreadPool::workerLoop() {
    std::function<void()> task;
    while (taskQueue_.pop(task) == 0) {
        task();
    }
}

#endif // THREAD_POOL_H