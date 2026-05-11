#ifndef TEST_H
#define TEST_H

#include <iostream>
#include <thread>
#include <future>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <algorithm>

#include "blocking_queue.h"
#include "thread_pool.h"

void TestBlockingQueue();

void TestThreadPoolV1_feasibility();
void TestThreadPoolV1_performance();
void TestThreadPoolVersion1();

void TestThreadPoolV2_feasibility();
void TestThreadPoolV2_performance();
void TestThreadPoolVersion2();

void TestThreadPoolV3_feasibility();
void TestThreadPoolV3_performance();
void TestThreadPoolVersion3();


#endif // TEST_H