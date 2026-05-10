#include <iostream>
#include <thread>
#include <future>
#include <chrono>

int slowTask() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 42;
}

int main() {
    std::future<int> result = std::async(std::launch::async, slowTask);

    auto status = result.wait_for(std::chrono::seconds(3));
    if (status == std::future_status::ready) {
        std::cout << "Result: " << result.get() << std::endl;
    } else {
        std::cout << "Task timeout, using fallback value" << std::endl;
    }

    return 0;
}