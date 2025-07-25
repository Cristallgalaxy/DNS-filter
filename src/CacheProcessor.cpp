/**
 * @file CacheProcessor.cpp
 * @brief 实现缓存处理主线程的逻辑，包括处理域名、更新缓存、触发上报等。
 *
 * 本模块是系统核心组件之一，负责从域名队列中取出待处理域名，执行缓存更新、
 * 状态标记和异步上报逻辑。通过线程池加速处理，支持安全终止机制。
 *
 * 使用场景：DNS嗅探器的后端缓存管理线程，连接 Redis 缓存、域名处理队列和上报器。
 */

#include <iostream>
#include <chrono>
#include <mutex>

#include "CacheProcessor.h"
#include "RedisDNSCache.h"
#include "ThreadPool.h"

void cache_processor(
    RedisDNSCache& cache,
    ThreadSafeQueue<std::string>& domain_queue,
    std::atomic<bool>& stop_processing,
    DomainReporter& reporter,
    ThreadPool& pool
) {
    std::mutex cache_mutex;  // 保证对 RedisDNSCache 操作的线程安全

    // 处理单个域名任务
    auto process_domain = [&](const std::string& domain) {
        try {
            std::lock_guard<std::mutex> lock(cache_mutex);

            // 查找域名是否已存在
            auto entry = cache.find(domain);
            if (!entry) {
                // 不存在则作为可疑域名加入（默认状态 FAKE + DROP）
                cache.insertOrUpdate(domain, DomainStatus::FAKE, DomainAction::DROP);
            } else {
                // 已存在则更新访问时间或重置 TTL
                cache.insertOrUpdate(domain, entry->status, entry->action);
            }

            // 达到阈值触发一次上报
            if (cache.getPendingReportCount() >= kReportThreshold) {
                reporter.try_report_domains(cache, kMaxRetryCount, kRetryDelay);
            }
        } catch (const std::exception& e) {
            std::cerr << "[worker] error: " << e.what() << std::endl;
        }
    };

    // 主循环：不断从队列中取出域名处理，直到收到 stop 信号
    while (!stop_processing.load(std::memory_order_acquire)) {
        std::string domain;
        domain_queue.wait_and_pop(domain);  // 阻塞式等待
        if (domain.empty()) continue;       // 空字符串为“唤醒退出”标志

        pool.enqueue(process_domain, domain);  // 异步处理域名任务
    }

    // 停止前稍作等待，允许线程池中的任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // 执行最后一次域名上报
    try {
        std::lock_guard<std::mutex> lock(cache_mutex);
        reporter.try_report_domains(cache, kMaxRetryCount, kRetryDelay);
    } catch (const std::exception& e) {
        std::cerr << "[cache_processor] final reporting failed: " << e.what() << std::endl;
    }

    std::cout << "[cache_processor] stopped\n";
}
