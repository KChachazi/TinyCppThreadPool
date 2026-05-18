#include "test.h"

void TestBlockingQueue() {
    BlockingQueue<int> queue;

    auto producerFunc = [](BlockingQueue<int>& q, int id) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(100, 1000);
        for (int i = 0; i < 5; i ++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            int item = dis(gen);
            std::cout << "Produced: " << item << " by producer " << id << std::endl;
            if (q.push(item) != QueueStatus::OK) {
                std::cout << "Failed to push item, queue stopped." << std::endl;
                break;
            }
        }
    };

    auto consumerFunc = [](BlockingQueue<int>& q) {
        int item;
        while (true) {
            QueueStatus result = q.pop(item);
            if (result == QueueStatus::OK) {
                std::cout << "Consumed: " << item << std::endl;
            } else if (result == QueueStatus::STOPPED) {
                std::cout << "Queue stopped, exiting consumer." << std::endl;
                break;
            }
        }
    };

    std::thread producer1(producerFunc, std::ref(queue), 1);
    std::thread producer2(producerFunc, std::ref(queue), 2);
    std::thread producer3(producerFunc, std::ref(queue), 3);

    std::thread consumer(consumerFunc, std::ref(queue));

    producer1.join();
    producer2.join();
    producer3.join();
    queue.stop();
    consumer.join();
}

void TestThreadPoolV1_feasibility() {
    std::cout << "[V1 feasibility]" << std::endl;
    ThreadPool pool(4);

    // 1. 不同返回类型的任务：参数靠 lambda 捕获塞进去，submit 本身是无参的
    int a = 5, b = 10;
    auto fInt = pool.submit([a, b] { return a + b; });
    auto fStr = pool.submit([] { return std::string("hello from pool"); });
    auto fDouble = pool.submit([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // 故意慢一点
        return 3.14;
    });

    std::cout << "  int    -> " << fInt.get()    << "  (expect 15)" << std::endl;
    std::cout << "  string -> " << fStr.get()    << "  (expect hello from pool)" << std::endl;
    std::cout << "  double -> " << fDouble.get() << "  (expect 3.14)" << std::endl;

    // 2. 批量提交：多个任务并发跑，future 的取值顺序和执行顺序无关
    std::vector<std::future<int>> squares;
    for (int i = 0; i < 8; i ++) {
        squares.push_back(pool.submit([i] { return i * i; }));
    }
    bool allOk = true;
    for (int i = 0; i < 8; i ++) {
        int got = squares[i].get();
        if (got != i * i) allOk = false;
        std::cout << "  " << i << "^2 = " << got << std::endl;
    }
    std::cout << "  batch result " << (allOk ? "OK" : "WRONG") << std::endl;

    // pool 在这里析构：stop() 通知队列关闭，join() 等所有 worker 退出
}

void TestThreadPoolV1_performance() {
    std::cout << "[V1 performance] batch square: single-thread vs thread-pool(4)" << std::endl;
    const int N = 2000000;

    // 计算单个 x*x 的调度开销过大，所以增加每个运算的计算量，降低调度开销占比
    auto compute = [](int x) -> long long {
        long long acc = 0;
        for (int k = 1; k <= 64; k ++) acc += static_cast<long long>(x) * x % (k + 1);
        return acc;
    };

    // ---- 基线：单线程 ----
    auto t0 = std::chrono::steady_clock::now();
    long long sumSingle = 0;
    for (int i = 0; i < N; i ++) sumSingle += compute(i);
    auto t1 = std::chrono::steady_clock::now();

    // ---- 线程池：按块切分提交，降低调度开销 ----
    auto t2 = std::chrono::steady_clock::now();
    long long sumPool = 0;
    {
        ThreadPool pool(4);
        const int chunk = 4096;
        std::vector<std::future<long long>> parts;
        for (int begin = 0; begin < N; begin += chunk) {
            int end = std::min(begin + chunk, N);
            parts.push_back(pool.submit([=] {
                long long s = 0;
                for (int i = begin; i < end; i ++) s += compute(i);
                return s;
            }));
        }
        for (auto& f : parts) sumPool += f.get();
    }
    auto t3 = std::chrono::steady_clock::now();

    auto msSingle = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto msPool   = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    std::cout << "  checksum: single=" << sumSingle << " pool=" << sumPool
              << (sumSingle == sumPool ? "  (match)" : "  (MISMATCH!)") << std::endl;
    std::cout << "  single-thread : " << msSingle << " ms" << std::endl;
    std::cout << "  thread-pool(4): " << msPool   << " ms" << std::endl;
    if (msPool > 0) {
        std::cout << "  speedup: " << static_cast<double>(msSingle) / msPool << "x" << std::endl;
    }
}


void TestThreadPoolVersion1() {
    TestThreadPoolV1_feasibility();
    TestThreadPoolV1_performance();
}

void TestThreadPoolV2_feasibility() {
    std::cout << "[V2 feasibility] - check shutdown behavior" << std::endl;

    auto task = [] { return 42; };
    ThreadPool pool(2);
    auto future = pool.submit(task);
    std::cout << "  submitted task, got future" << std::endl;

    pool.shutdown();
    std::cout << "  called shutdown on pool" << std::endl;
    try {
        auto future2 = pool.submit(task);
        std::cout << "  ERROR: was able to submit task after shutdown!" << std::endl;
    } catch (const std::exception& ex) {
        std::cout << ex.what() << std::endl;
    }
}

void TestThreadPoolV2_performance() {
    std::cout << "[V2 performance] - steady-state behavior (300000 common tasks)" << std::endl;

    ThreadPool pool(4);
    const int numTasks = 300000;
    int submitted = 0;
    std::vector<std::future<int>> futures;

    auto submitTask = [&pool, numTasks, &submitted, &futures]() {
        for (int i = 0; i < numTasks; i ++) {
            try {
                futures.push_back(pool.submit([i] { return i * i; }));
                submitted++;
            } catch (const std::exception& ex) {
                std::cout << "  ERROR: failed to submit task " << i << ": " << ex.what() << std::endl;
                break;
            }
        }
    };

    std::thread submitter(submitTask);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // 确保线程池启动完成
    pool.shutdown();
    std::cout << "  called shutdown on pool" << std::endl;
    try {
        pool.shutdown();
    } catch (const std::exception& ex) {
        std::cout << "  ERROR: failed to shutdown pool: " << ex.what() << std::endl;
    }
    std::cout << "  shutdown called again (should be no-op)" << std::endl;

    submitter.join();

    std::cout << "  submitted " << submitted << " tasks before shutdown" << std::endl;
    bool allOk = true;
    for (int i = 0; i < submitted; i ++) {
        int got = futures[i].get();
        if (got != i * i) {
            allOk = false;
            std::cout << "  ERROR: task " << i << " expected " << i * i << " but got " << got << std::endl;
        }
    }

    std::cout << "  all previously submitted tasks completed: " << (allOk ? "OK" : "WRONG") << std::endl;
}

void TestThreadPoolVersion2() {
    TestThreadPoolV2_feasibility();
    TestThreadPoolV2_performance();
}

// =============================================================================
// V3 测试工具
// =============================================================================

namespace {

int v3_passed = 0;
int v3_failed = 0;

void v3_reset() {
    v3_passed = 0;
    v3_failed = 0;
}

void v3_expect(bool cond, const std::string& name) {
    if (cond) {
        v3_passed++;
        std::cout << "    [PASS] " << name << std::endl;
    } else {
        v3_failed++;
        std::cout << "    [FAIL] " << name << std::endl;
    }
}

void v3_report(const char* group) {
    std::cout << "  --- " << group << ": "
              << v3_passed << "/" << (v3_passed + v3_failed) << " passed ---"
              << std::endl;
}

// 看门狗：超时直接 abort，避免某个 case 死锁卡住整个测试。
// 用法：函数开头创建一个局部对象，正常返回前会自动 cancel。
class Watchdog {
public:
    Watchdog(int timeoutSec, std::string what)
        : timeoutSec_(timeoutSec), what_(std::move(what)) {
        thread_ = std::thread([this] {
            for (int i = 0; i < timeoutSec_ * 10; i++) {
                if (done_.load()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::cerr << "[WATCHDOG] '" << what_ << "' timed out after "
                      << timeoutSec_ << "s, aborting." << std::endl;
            std::abort();
        });
    }

    ~Watchdog() {
        done_.store(true);
        if (thread_.joinable()) thread_.join();
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    int timeoutSec_;
    std::string what_;
    std::atomic<bool> done_{false};
    std::thread thread_;
};

} // anonymous namespace


// =============================================================================
// V3 feasibility: F1 ~ F8
// =============================================================================

// F1：异常隔离基本面
//   - pool(2)，submit 5 个任务，第 2、4 个 throw std::runtime_error("F1-boom-i")
//   - 断言 (a)：抛异常的 future.get() 重抛同一异常（catch 住并比较 what()）
//   - 断言 (b)：其余 3 个 future.get() 返回正确值（i*10 之类）
//   - 断言 (c)：之后再 submit 一个普通任务，能正常执行 → 证明 worker 没死
static void f1_exception_isolation_basic() {
    std::cout << "[F1] exception isolation - basic" << std::endl;
    Watchdog wd(5, "F1");

    ThreadPool pool(2);

    // i ∈ {0, 2, 4} 抛异常，i ∈ {1, 3} 正常返回 i*10
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 5; i++) {
        if (i % 2 == 0) {
            // 显式 -> int：throw 路径无 return 语句，不写返回类型会被推成 void
            futures.push_back(pool.submit([i]() -> int {
                throw std::runtime_error("F1-boom-" + std::to_string(i));
            }));
        } else {
            futures.push_back(pool.submit([i] { return i * 10; }));
        }
    }

    // 断言 (a)：抛异常的 future.get() 重抛同一异常，且 what() 内容匹配
    for (int i : {0, 2, 4}) {
        bool ok = false;
        try {
            futures[i].get();
        } catch (const std::runtime_error& e) {
            ok = (std::string(e.what()) == "F1-boom-" + std::to_string(i));
        } catch (...) {
            // 类型错了，ok 保持 false
        }
        v3_expect(ok, "F1.a: throwing task " + std::to_string(i) + " rethrows runtime_error");
    }

    // 断言 (b)：正常任务的 future.get() 返回正确值
    for (int i : {1, 3}) {
        bool ok = false;
        try {
            ok = (futures[i].get() == i * 10);
        } catch (...) {
            // 不该抛
        }
        v3_expect(ok, "F1.b: normal task " + std::to_string(i) + " returns " + std::to_string(i * 10));
    }

    // 断言 (c)：worker 没死——池子还能继续接新任务并执行
    bool laterOk = false;
    try {
        auto laterFuture = pool.submit([] { return 42; });
        laterOk = (laterFuture.get() == 42);
    } catch (...) {
        // 不该抛
    }
    v3_expect(laterOk, "F1.c: pool still works after task exceptions (worker alive)");
}

// F2：自定义 handler 在 submit 路径上不触发（与文档描述一致的负向测试）
//   - pool(2)，setExceptionHandler 装一个把 std::atomic<int> hookFired 自增的 handler
//   - submit 一个 throw 的任务，future.get() 应该重抛
//   - 断言：hookFired == 0（packaged_task 把异常吃掉，钩子根本不会被调）
//   - 备注：要真正触发钩子，需要后续 post()/fire-and-forget 接口；现在这条
//     主要用作回归——如果未来重构 submit 走 promise+try/catch，此测试会变红，
//     提醒把文档跟代码同步。
static void f2_handler_not_fired_for_submit() {
    std::cout << "[F2] custom handler not fired on submit path" << std::endl;
    Watchdog wd(5, "F2");
    ThreadPool pool(2);
    std::atomic<int> hookFired{0};
    pool.setExceptionHandler([&hookFired](std::exception_ptr) {
        hookFired.fetch_add(1);
    });
    pool.submit([] { throw std::runtime_error("F2-boom"); }).wait();
    v3_expect(hookFired.load() == 0, "F2: exception handler not fired for submit exceptions");
}

// F3：shutdown 之后 submit 抛 ThreadPoolStopped（异常分类生效）
//   - pool(2) → shutdown() → submit(...)
//   - 用 catch (const ThreadPoolStopped&) 精确接住，证明类型分类有用
//   - 反例：catch (const ThreadPoolFull&) 不能接住——可在另一个独立 case 验证
static void f3_shutdown_rejects_with_typed_exception() {
    std::cout << "[F3] submit after shutdown throws ThreadPoolStopped" << std::endl;
    Watchdog wd(5, "F3");
    ThreadPool pool(2);
    pool.shutdown();
    try {
        pool.submit([] { return 1; });
        v3_expect(false, "F3: submit after shutdown should throw");
    } catch (const ThreadPoolStopped&) {
        v3_expect(true, "F3: submit after shutdown throws ThreadPoolStopped");
    } catch (const std::exception& ex) {
        v3_expect(false, "F3: submit after shutdown threw wrong exception: " + std::string(ex.what()));
    } catch (...) {
        v3_expect(false, "F3: submit after shutdown threw unknown exception");
    }
}

// F4：shutdownNow 丢弃未执行任务，被丢任务的 future 抛 broken_promise
//   - pool(2, 16)，submit 50 个 sleep 200ms 的任务，主线程 sleep 50ms 后 shutdownNow()
//   - 断言 (a)：至少前 2 个 future 正常返回（worker 已经在跑了，不丢）
//   - 断言 (b)：后续大部分 future.get() 抛 std::future_error，且
//                e.code() == std::future_errc::broken_promise
//   - 断言 (c)：实际执行数 < 提交数（用 atomic<int> executed 计数）
static void f4_shutdownNow_drops_pending() {
    std::cout << "[F4] shutdownNow drops pending tasks (broken_promise)" << std::endl;
    Watchdog wd(10, "F4");

    ThreadPool pool(2, 16);
    std::vector<std::future<void>> futures;
    std::atomic<int> executed{0};

    const int numTasks = 50;
    for (int i = 0; i < numTasks; i ++) {
        futures.push_back(pool.submit([&executed] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            executed.fetch_add(1);
        }));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pool.shutdownNow();

    // (a) 至少前 2 个 future 正常返回（worker 已经在跑了，不丢）
    // (b) 后续大部分 future.get() 抛 std::future_error，且 e.code() == std::future_errc::broken_promise
    int okCount = 0, brokenCount = 0;
    for (int i = 0; i < numTasks; i ++) {
        try {
            futures[i].get();
            okCount++;
        } catch (std::future_error& e) {
            if (e.code() == std::make_error_code(std::future_errc::broken_promise)) {
                brokenCount++;
            } else {
                v3_expect(false, "F4: future " + std::to_string(i) + " threw unexpected future_error: " + e.what());
            }
        } catch (const std::exception& ex) {
            v3_expect(false, "F4: future " + std::to_string(i) + " threw wrong exception: " + std::string(ex.what()));
        } catch (...) {
            v3_expect(false, "F4: future " + std::to_string(i) + " threw unknown exception");
        }
    }
    v3_expect(okCount >= 2, "F4: at least 2 futures should complete successfully");
    v3_expect(brokenCount > 0, "F4: at least some futures should be broken");
    // (c) 实际执行数 < 提交数（用 atomic<int> executed 计数）
    int executedCount = executed.load();
    v3_expect(executedCount < numTasks, "F4: executed count should be less than submitted count");
}

// F5：shutdown / shutdownNow 互斥幂等
//   - 4 种顺序各跑一次：
//       (a) shutdown → shutdown
//       (b) shutdown → shutdownNow
//       (c) shutdownNow → shutdown
//       (d) shutdownNow → shutdownNow
//   - 每种都不抛、不死锁；析构时再隐式调一次 shutdown 也无事
//   - 用 Watchdog 兜底：如果某种顺序死锁，会被 abort
static void f5_shutdown_variants_idempotent_and_exclusive() {
    std::cout << "[F5] shutdown/shutdownNow idempotent + first-wins" << std::endl;
    Watchdog wd(5, "F5");
    // (a) shutdown → shutdown
    {
        ThreadPool pool(2);
        pool.shutdown();
        try {
            pool.shutdown();
            v3_expect(true, "F5.a: shutdown followed by shutdown does not throw");
        } catch (const std::exception& ex) {
            v3_expect(false, "F5.a: shutdown followed by shutdown threw exception: " + std::string(ex.what()));
        } catch (...) {
            v3_expect(false, "F5.a: shutdown followed by shutdown threw unknown exception");
        }
    }
    // (b) shutdown → shutdownNow
    {
        ThreadPool pool(2);
        pool.shutdown();
        try {
            pool.shutdownNow();
            v3_expect(true, "F5.b: shutdown followed by shutdownNow does not throw");
        } catch (const std::exception& ex) {
            v3_expect(false, "F5.b: shutdown followed by shutdownNow threw exception: " + std::string(ex.what()));
        } catch (...) {
            v3_expect(false, "F5.b: shutdown followed by shutdownNow threw unknown exception");
        }
    }
    // (c) shutdownNow → shutdown
    {
        ThreadPool pool(2);
        pool.shutdownNow();
        try {
            pool.shutdown();
            v3_expect(true, "F5.c: shutdownNow followed by shutdown does not throw");
        } catch (const std::exception& ex) {
            v3_expect(false, "F5.c: shutdownNow followed by shutdown threw exception: " + std::string(ex.what()));
        } catch (...) {
            v3_expect(false, "F5.c: shutdownNow followed by shutdown threw unknown exception");
        }
    }
    // (d) shutdownNow → shutdownNow
    {
        ThreadPool pool(2);
        pool.shutdownNow();
        try {
            pool.shutdownNow();
            v3_expect(true, "F5.d: shutdownNow followed by shutdownNow does not throw");
        } catch (const std::exception& ex) {
            v3_expect(false, "F5.d: shutdownNow followed by shutdownNow threw exception: " + std::string(ex.what()));
        } catch (...) {
            v3_expect(false, "F5.d: shutdownNow followed by shutdownNow threw unknown exception");
        }
    }
}

// F6：trySubmit 在队列满时抛 ThreadPoolFull
//   - pool(1, 4)：1 个 worker，队列容量 4
//   - 第 1 个 submit 一个 sleep 800ms 的长任务（占住 worker）
//   - sleep 50ms 让 worker 把它取走
//   - 然后 trySubmit 4 个空任务（应当全部成功，把队列填满）
//   - 第 6 个 trySubmit 应抛 ThreadPoolFull
//   - 收尾：shutdown，让长任务跑完
static void f6_trySubmit_throws_when_full() {
    std::cout << "[F6] trySubmit throws ThreadPoolFull on full queue" << std::endl;
    Watchdog wd(5, "F6");
    
    ThreadPool pool(1, 4);
    pool.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(800)); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bool allAccepted = true;
    for (int i = 0; i < 4; i ++) {
        try {
            pool.trySubmit([] {});
        } catch (const ThreadPoolFull&) {
            allAccepted = false;
            v3_expect(false, "F6: trySubmit #" + std::to_string(i) + " should succeed");
        } catch (const std::exception& ex) {
            allAccepted = false;
            v3_expect(false, "F6: trySubmit #" + std::to_string(i) + " threw wrong exception: " + std::string(ex.what()));
        } catch (...) {
            allAccepted = false;
            v3_expect(false, "F6: trySubmit #" + std::to_string(i) + " threw unknown exception");
        }
    }
    if (allAccepted) {
        v3_expect(true, "F6: all trySubmit calls succeeded");
    }

    try {
        pool.trySubmit([] {});
        v3_expect(false, "F6: trySubmit #5 should throw ThreadPoolFull");
    } catch (const ThreadPoolFull&) {
        v3_expect(true, "F6: trySubmit #5 threw ThreadPoolFull as expected");
    } catch (const std::exception& ex) {
        v3_expect(false, "F6: trySubmit #5 threw wrong exception: " + std::string(ex.what()));
    } catch (...) {
        v3_expect(false, "F6: trySubmit #5 threw unknown exception");
    }
}

// F7：submit 在队列满时阻塞（背压）
//   - pool(1, 2)：1 个 worker，队列容量 2，每个任务 sleep 100ms
//   - 主线程连续 submit 6 个任务，记录每次 submit 的耗时
//   - 断言：前 3 次 submit 几乎瞬时（worker 正在跑 + 队列容量 2 = 3 个槽）
//   - 断言：后续若干次 submit 出现 ~100ms 量级的阻塞（背压生效）
//   - 容差：用 stable steady_clock，阈值放宽（>50ms 就算阻塞）以减少 flake
static void f7_submit_blocks_when_full() {
    std::cout << "[F7] submit blocks (backpressure) when queue full" << std::endl;
    Watchdog wd(10, "F7");
    
    ThreadPool pool(1, 2);
    std::vector<long long> submitTimes;
    for (int i = 0; i < 6; i ++) {
        auto start = std::chrono::steady_clock::now();
        pool.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        submitTimes.push_back(ms);
    }
    // 断言：前 3 次 submit 几乎瞬时（worker 正在跑 + 队列容量 2 = 3 个槽）
    for (int i = 0; i < 3; i ++) {
        v3_expect(submitTimes[i] < 50, "F7: submit #" + std::to_string(i) + " should be fast (ms=" + std::to_string(submitTimes[i]) + ")");
    }
    // 断言：后续若干次 submit 出现 ~100ms 量级的阻塞（背压生效）
    for (int i = 3; i < 6; i ++) {
        v3_expect(submitTimes[i] >= 50, "F7: submit #" + std::to_string(i) + " should be blocked (ms=" + std::to_string(submitTimes[i]) + ")");
    }
}

// F8：关闭时唤醒阻塞在 push 上的生产者（不死锁）
//   - pool(1, 2)，worker 跑一个 sleep 2s 的任务把自己堵住
//   - 起一个生产者线程：在循环里 submit 直到抛异常或满 1000 次
//   - 主线程 sleep 100ms，确认生产者已被卡在 push 上（队列已满）
//   - 调 shutdown()
//   - join 生产者线程；断言生产者退出原因是抛了 ThreadPoolStopped（不是死锁，不是别的异常）
//   - Watchdog 兜底：3s 内必须结束
static void f8_shutdown_wakes_blocked_push() {
    std::cout << "[F8] shutdown wakes producers blocked on push" << std::endl;
    Watchdog wd(5, "F8");

    ThreadPool pool(1, 2);
    pool.submit([] { std::this_thread::sleep_for(std::chrono::seconds(2)); });
    std::thread producer([&pool] {
        int submitCount = 0;
        try {
            while (submitCount < 1000) {
                pool.submit([] {});
                submitCount++;
            }
        } catch (const ThreadPoolStopped&) {
            v3_expect(true, "F8: producer thread exited with ThreadPoolStopped as expected");
        } catch (const std::exception& ex) {
            v3_expect(false, "F8: producer thread threw wrong exception: " + std::string(ex.what()));
        } catch (...) {
            v3_expect(false, "F8: producer thread threw unknown exception");
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pool.shutdown();
    producer.join();
}


// =============================================================================
// V3 stability: S1 ~ S2
// =============================================================================

// S1：高并发混合负载 + shutdown（不丢已接收的任务）
//   - pool(4, 256)
//   - 4 个生产者线程各 submit 50000 个轻任务（task 体只做 executed.fetch_add(1)）
//   - 主线程 sleep 100ms 后 shutdown()
//   - 每个生产者维护自己的 accepted / rejected 计数
//   - 断言：sum(accepted) == executed（已接收的全跑了，没丢）
//   - 断言：sum(accepted) + sum(rejected) == 4 * 50000
//   - Watchdog：30s
static void s1_stability_shutdown_no_loss() {
    std::cout << "[S1] stability under concurrent load + shutdown" << std::endl;
    Watchdog wd(30, "S1");
    
    ThreadPool pool(4, 256);
    const int numProducers = 4;
    const int tasksPerProducer = 50000;
    std::vector<std::thread> producers;
    std::vector<std::atomic<int>> accepted(numProducers);
    std::vector<std::atomic<int>> rejected(numProducers);
    std::atomic<int> executed{0};

    for (int p = 0; p < numProducers; p ++) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < tasksPerProducer; i ++) {
                try {
                    pool.submit([&executed] { executed.fetch_add(1); });
                    accepted[p].fetch_add(1);
                } catch (const ThreadPoolStopped&) {
                    rejected[p].fetch_add(1);
                } catch (...) {
                    // 其他异常不应该发生
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pool.shutdown();
    for (auto& t : producers) t.join();

    int totalAccepted = 0, totalRejected = 0;
    for (int p = 0; p < numProducers; p ++) {
        totalAccepted += accepted[p].load();
        totalRejected += rejected[p].load();
    }
    v3_expect(totalAccepted + totalRejected == numProducers * tasksPerProducer, "S1: total tasks submitted matches expected");
    v3_expect(totalAccepted == executed.load(), "S1: all accepted tasks were executed");
}

// S2：同 S1 负载，但中途换 shutdownNow
//   - 断言：executed < accepted（有任务被丢）
//   - 断言：未执行任务的 future 全是 broken_promise
//     —— 这条要求生产者把 future 也存下来才能检查；可以只存一部分（前 N 个）
//        减少内存占用
static void s2_stability_shutdownNow_drops() {
    std::cout << "[S2] stability under concurrent load + shutdownNow" << std::endl;
    Watchdog wd(30, "S2");
    
    ThreadPool pool(4, 256);
    const int numProducers = 4;
    const int tasksPerProducer = 50000;
    std::vector<std::thread> producers;
    std::vector<std::atomic<int>> accepted(numProducers);
    std::vector<std::atomic<int>> rejected(numProducers);
    std::atomic<int> executed{0};

    // 每个生产者只存前 100 个 future 来检查，避免内存占用过大
    const int futuresToCheck = 100;
    std::vector<std::vector<std::future<void>>> futures(numProducers);

    for (int p = 0; p < numProducers; p ++) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < tasksPerProducer; i ++) {
                try {
                    auto f = pool.submit([&executed] { executed.fetch_add(1); });
                    if (i < futuresToCheck) {
                        futures[p].push_back(std::move(f));
                    }
                    accepted[p].fetch_add(1);
                } catch (const ThreadPoolStopped&) {
                    rejected[p].fetch_add(1);
                } catch (...) {
                    // 其他异常不应该发生
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    pool.shutdownNow();
    for (auto& t : producers) t.join();

    int totalAccepted = 0, totalRejected = 0;
    for (int p = 0; p < numProducers; p ++) {
        totalAccepted += accepted[p].load();
        totalRejected += rejected[p].load();
    }
    v3_expect(totalAccepted + totalRejected == numProducers * tasksPerProducer, "S2: total tasks submitted matches expected");
    v3_expect(executed.load() < totalAccepted, "S2: some accepted tasks were dropped by shutdownNow");
}


// =============================================================================
// V3 performance: P1
// =============================================================================

// P1：背压效果对比
//   - 任务总量固定（如 200000 个轻任务，每个忙等 ~10us 模拟轻计算）
//   - 三档容量：unbounded(0) / 1024 / 16
//   - 单个生产者线程 submit 全部任务，记录：
//       (a) 总耗时 —— 三档应当差不多（稳态吞吐不该被 capacity 拖累太多）
//       (b) 主线程每次 submit 的耗时分位数（P50 / P99）
//             unbounded：几乎都是 0
//             16：P99 显著上升 —— 这就是背压把生产者节流了
//   - 不需要硬性断言，打印对比表，肉眼看趋势即可
static void p1_backpressure_comparison() {
    std::cout << "[P1] backpressure: capacity 0 vs 1024 vs 16" << std::endl;
    Watchdog wd(60, "P1");

    const int numTasks = 200000;
    auto computeTask = [] {
        volatile int x = 0;
        for (int i = 0; i < 100; i ++) {
            x += i;
        }
    };
    // unbounded(0)
    {
        ThreadPool pool(4, 0);
        std::vector<long long> submitTimes;
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < numTasks; i ++) {
            auto submitStart = std::chrono::steady_clock::now();
            pool.submit(computeTask);
            auto submitEnd = std::chrono::steady_clock::now();
            submitTimes.push_back(std::chrono::duration_cast<std::chrono::microseconds>(submitEnd - submitStart).count());
        }
        auto end = std::chrono::steady_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::sort(submitTimes.begin(), submitTimes.end());
        std::cout << "  capacity=0 (unbounded): total=" << totalMs << " ms, P50=" << submitTimes[submitTimes.size() / 2]
                  << " us, P99=" << submitTimes[submitTimes.size() * 99 / 100] << " us" << std::endl;
    }
    // capacity=1024
    {
        ThreadPool pool(4, 1024);
        std::vector<long long> submitTimes;
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < numTasks; i ++) {
            auto submitStart = std::chrono::steady_clock::now();
            pool.submit(computeTask);
            auto submitEnd = std::chrono::steady_clock::now();
            submitTimes.push_back(std::chrono::duration_cast<std::chrono::microseconds>(submitEnd - submitStart).count());
        }
        auto end = std::chrono::steady_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::sort(submitTimes.begin(), submitTimes.end());
        std::cout << "  capacity=1024: total=" << totalMs << " ms, P50=" << submitTimes[submitTimes.size() / 2]
                  << " us, P99=" << submitTimes[submitTimes.size() * 99 / 100] << " us" << std::endl;
    }
    // capacity=16
    {
        ThreadPool pool(4, 16);
        std::vector<long long> submitTimes;
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < numTasks; i ++) {
            auto submitStart = std::chrono::steady_clock::now();
            pool.submit(computeTask);
            auto submitEnd = std::chrono::steady_clock::now();
            submitTimes.push_back(std::chrono::duration_cast<std::chrono::microseconds>(submitEnd - submitStart).count());
        }
        auto end = std::chrono::steady_clock::now();
        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::sort(submitTimes.begin(), submitTimes.end());
        std::cout << "  capacity=16: total=" << totalMs << " ms, P50=" << submitTimes[submitTimes.size() / 2]
                  << " us, P99=" << submitTimes[submitTimes.size() * 99 / 100] << " us" << std::endl;
    }
}


// =============================================================================
// 分发入口
// =============================================================================

void TestThreadPoolV3_feasibility() {
    std::cout << "[V3 feasibility]" << std::endl;
    v3_reset();
    f1_exception_isolation_basic();
    f2_handler_not_fired_for_submit();
    f3_shutdown_rejects_with_typed_exception();
    f4_shutdownNow_drops_pending();
    f5_shutdown_variants_idempotent_and_exclusive();
    f6_trySubmit_throws_when_full();
    f7_submit_blocks_when_full();
    f8_shutdown_wakes_blocked_push();
    v3_report("V3 feasibility");
}

void TestThreadPoolV3_performance() {
    std::cout << "[V3 performance]" << std::endl;
    v3_reset();
    s1_stability_shutdown_no_loss();
    s2_stability_shutdownNow_drops();
    p1_backpressure_comparison();
    v3_report("V3 performance");
}

void TestThreadPoolVersion3() {
    TestThreadPoolV3_feasibility();
    TestThreadPoolV3_performance();
}