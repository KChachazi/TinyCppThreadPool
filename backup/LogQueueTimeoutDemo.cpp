#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <random>

class LogQueue {
public:
    void push(const std::string& log) {
        std::lock_guard<std::mutex> lock(mtx);
        logqueue.push(log);
        cv.notify_one();
    }

    int pop(std::string& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mtx);
        bool hasLog = cv.wait_for(lock, timeout, [this] { return !logqueue.empty() || closed; });
        if (hasLog && !logqueue.empty()) {
            out = logqueue.front();
            logqueue.pop();
            return 0; // Success
        }
        if (closed) {
            return 2; // Queue closed
        }
        return 1; // Timeout
    }

    bool isEmpty() {
        std::lock_guard<std::mutex> lock(mtx);
        return logqueue.empty();
    }

    bool isClosed() {
        std::lock_guard<std::mutex> lock(mtx);
        return closed;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx);
        closed = true;
        cv.notify_all();
    }

private:
    std::queue<std::string> logqueue;
    std::mutex mtx;
    std::condition_variable cv;
    bool closed = false;
};

void logProducer(LogQueue& q, int id) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(80, 180);
    for (int i = 0; i < 10; i ++) {
        q.push("Log message " + std::to_string(i) + " - From thread " + std::to_string(id));
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
    }
}

void logConsumer(LogQueue& q) {
    std::string log;
    int unknown_error_count = 0;
    while (true) {
        int result = q.pop(log, std::chrono::milliseconds(100));
        if (result == 0) {
            std::cout << "[LOG]" << log << std::endl;
        } else if (result == 1) {
            std::cout << "[WAIT TIMEOUT]" << std::endl;
        } else if (result == 2) {
            std::cout << "[LOG QUEUE CLOSED]" << std::endl;
            break;
        } else {
            std::cout << "[UNKNOWN ERROR]" << std::endl;
            unknown_error_count++;
             if (unknown_error_count >= 3) {
                std::cout << "[TOO MANY UNKNOWN ERRORS, EXITING]" << std::endl;
                break;
            }
        }
    }
    
    if (q.isEmpty() && q.isClosed()) {
        std::cout << "[CONSUMER EXIT]" << std::endl;
    }
}

int main() {

    LogQueue logQueue;
    std::thread producer1(logProducer, std::ref(logQueue), 1);
    std::thread producer2(logProducer, std::ref(logQueue), 2);
    std::thread consumer(logConsumer, std::ref(logQueue));
    producer1.join();
    producer2.join();
    logQueue.close();
    consumer.join();
    return 0;
}