#include <iostream>
#include <chrono>
#include <thread>
#include <condition_variable>

#include "StatsProcessor.h"
#include "RedisDNSCache.h"

static std::condition_variable stats_cv;
static std::mutex stats_mutex;

void stats_processor(
    RedisDNSCache& cache,
    std::atomic<bool>& stop_processing,
    DomainReporter& reporter,
    int interval_seconds
) {
    std::unique_lock<std::mutex> lock(stats_mutex);

    while (!stop_processing.load(std::memory_order_acquire)) {
        // 等待 interval_seconds 或收到退出通知
        if (stats_cv.wait_for(lock, std::chrono::seconds(interval_seconds),
                              [&stop_processing]() { return stop_processing.load(); })) {
            break; // 收到唤醒且 stop_processing = true，退出线程
        }

        reporter.reportStats(cache, interval_seconds);
        cache.printAllData();
    }

    std::cout << "[StatsProcessor] Exiting stats thread\n";
}

void stop_stats_report() {
    stats_cv.notify_all();
}
