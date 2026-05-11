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
            if (q.push(item) != 0) {
                std::cout << "Failed to push item, queue stopped." << std::endl;
                break;
            }
        }
    };

    auto consumerFunc = [](BlockingQueue<int>& q) {
        int item;
        while (true) {
            int result = q.pop(item);
            if (result == 0) {
                std::cout << "Consumed: " << item << std::endl;
            } else if (result == ERROR_QUEUE_STOPPED) {
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