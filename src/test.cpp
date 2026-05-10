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
