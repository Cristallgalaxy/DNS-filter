#ifndef STATS_PROCESSOR_H
#define STATS_PROCESSOR_H

#pragma once

#include <atomic>  // 用于 std::atomic 类型
#include "RedisDNSCache.h"
#include "DomainReporter.h"

/**
 * @brief 统计上报处理线程主函数
 * 
 * 在指定的间隔时间内（interval_seconds），定期从 RedisDNSCache 中
 * 获取域名访问数据，并通过 DomainReporter 上报至远程服务器。
 * 
 * 该函数通常在单独线程中运行，直到 stop_processing 变为 true 后退出。
 * 
 * @param cache 引用 RedisDNSCache 实例，用于访问缓存数据
 * @param stop_processing 线程退出标志，外部设置为 true 后线程安全退出
 * @param reporter 用于上报数据的 DomainReporter 单例实例
 * @param interval_seconds 上报的时间间隔，单位为秒
 */
void stats_processor(
    RedisDNSCache& cache,
    std::atomic<bool>& stop_processing,
    DomainReporter& reporter,
    int interval_seconds
);

/**
 * @brief 主动停止 stats_processor 线程的辅助函数
 * 
 * 如果 stats_processor 使用了条件变量或其他阻塞机制等待唤醒，
 * 该函数应通知或唤醒对应的条件变量，使线程尽快退出。
 * 
 * 该函数的实现细节依赖于具体的同步机制设计。
 */
void stop_stats_report();

#endif // STATS_PROCESSOR_H
