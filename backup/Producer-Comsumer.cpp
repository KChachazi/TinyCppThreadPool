#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <random>
#include <queue>
#include <condition_variable>

using namespace std;
const int CAPACITY = 10;

class SafeQueue {
public:
    void push(int value) {
        unique_lock<mutex> lock(mtx);
        // Wait until queue has space or queue is closed.
        not_full.wait(lock, [this] {return q.size() < capacity;});
        q.push(value);
        not_empty.notify_one();
    }

    bool pop(int& value) {
        unique_lock<mutex> lock(mtx);
        // Wake up when data arrives or when production is fully closed.
        not_empty.wait(lock, [this] {return !q.empty() || closed; });

        // If queue is empty and closed, consumer should exit.
        if (q.empty()) {
            return false;
        }

        value = q.front();
        q.pop();
        not_full.notify_one();
        return true;
    }

    void close() {
        unique_lock<mutex> lock(mtx);
        closed = true;
        // Wake all waiting consumers so they can observe "closed" and exit.
        not_empty.notify_all();
    }

    bool try_pop(int& value) {
        unique_lock<mutex> lock(mtx);
        bool success = not_empty.wait_for(lock, chrono::milliseconds(100), [this] {return !q.empty(); });
        if (success) {
            value = q.front();
            q.pop();
            not_full.notify_one();
        }
        return success;
    }

private:
    queue<int> q;
    int capacity = CAPACITY;
    bool closed = false;
    mutex mtx;
    condition_variable not_full;
    condition_variable not_empty;
};

void Producer(SafeQueue& q, int id, int amount) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(1, 100);
    for (int i = 0; i < amount; i ++) {
        int value = dist(gen);
        cout << "Producer " << id << " produced: " << value << endl;
        q.push(value);
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void Consumer(SafeQueue& q, int id) {
    int value;
    while (q.pop(value)) {
        std::cout << "Consumer " << id << " consumed: " << value << std::endl;
    }
    std::cout << "Consumer " << id << " exits." << std::endl;
}

int main() {
    SafeQueue q;

    std::thread p1(Producer, std::ref(q), 1, 10), p2(Producer, std::ref(q), 2, 10);
    std::thread c1(Consumer, std::ref(q), 1), c2(Consumer, std::ref(q), 2);

    p1.join();
    p2.join();

    // Producers are done; close queue and let consumers drain then exit.
    q.close();

    c1.join();
    c2.join();

    return 0;
}