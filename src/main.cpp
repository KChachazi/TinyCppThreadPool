#include <iostream>
#include <thread>
#include <future>
#include <chrono>

#include "blocking_queue.h"
#include "test.h"

int main() {

    TestThreadPoolVersion1();
    TestThreadPoolVersion2();
    TestThreadPoolVersion3();

    return 0;
}