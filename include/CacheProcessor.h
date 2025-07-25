#ifndef CACHE_PROCESSOR_H
#define CACHE_PROCESSOR_H
#pragma once

#include <atomic>
#include <unordered_map>
#include <chrono>
#include "RedisDNSCache.h"
#include "ThreadSafeQueue.h"
#include "DomainReporter.h"
#include "ThreadPool.h"

/**
 * @file cache_processor.h
 * @brief 声明了 DNS 缓存处理模块的主处理函数。
 *
 * 本模块用于从域名处理队列中提取待处理域名，结合 RedisDNSCache 进行缓存判断与更新，
 * 并通过线程池异步上报指定状态的域名（如 FAKE、PEND 状态）。
 *
 * 典型应用场景包括 DNS 过滤器、入侵检测系统等，适用于需要将捕获的域名进行缓存与分类上报的系统。
 */

/**
 * @brief 每批触发异步上报的最小域名数量。
 *
 * 当聚集到的待上报域名数量 ≥ kReportThreshold 时，将提交一个异步上报任务。
 */
constexpr size_t kReportThreshold = 5;

/**
 * @brief 每个域名上报失败后的最大重试次数。
 *
 * 若在上报过程中发生失败（如网络错误），最多重试 kMaxRetryCount 次。
 */
constexpr size_t kMaxRetryCount = 3;

/**
 * @brief 每次重试之间的等待时间。
 *
 * 例如第一次失败后等待 kRetryDelay 时间后再次尝试。
 */
constexpr auto kRetryDelay = std::chrono::seconds(5);

/**
 * @brief 缓存处理主线程函数。
 *
 * 持续从线程安全队列中获取域名，结合 RedisDNSCache 判断状态，并控制是否异步上报。
 * 适用于与抓包线程并行执行，具有一定的容错能力和退出机制。
 *
 * @param cache RedisDNSCache 实例，用于查询和更新域名状态。
 * @param domain_queue 域名输入队列，由抓包模块填充，多个线程可并发写入。
 * @param stop_processing 原子标志，若为 true 则终止处理循环。
 * @param reporter DomainReporter 实例，用于执行上报逻辑（如 HTTP POST）。
 * @param pool ThreadPool 实例，用于异步执行上报任务，避免阻塞主线程。
 */
void cache_processor(
    RedisDNSCache& cache,
    ThreadSafeQueue<std::string>& domain_queue,
    std::atomic<bool>& stop_processing,
    DomainReporter& reporter,
    ThreadPool& pool
);

#endif // CACHE_PROCESSOR_H
