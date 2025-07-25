#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <string>

#include "pcap_capture.h"
#include "RedisDNSCache.h"
#include "CacheProcessor.h"
#include "ThreadSafeQueue.h"
#include "ThreadPool.h"
#include "DomainReporter.h"
#include "StatsProcessor.h"

// 全局变量
std::atomic<bool> stop_processing(false);
ThreadSafeQueue<std::string> domain_queue;

void signal_handler(int signal) {
    stop_processing = true;
    stop_packet_capture();  // 主动打断 pcap 抓包线程
    stop_stats_report();
    domain_queue.push("");  // 唤醒处理线程以便退出
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <network_interface>\n"
                  << "Example: " << argv[0] << " lo\n";
        return 1;
    }

    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string device = argv[1];

    try {
        auto& reporter = DomainReporter::getInstance();
        reporter.setServerUrl("http://localhost:8080/hello");
        // 初始化Redis缓存
        RedisDNSCache cache(10);
        
        // 启动缓存处理线程
        ThreadPool pool(std::thread::hardware_concurrency());
        // ThreadPool pool(1);
        std::thread cache_thread(cache_processor, std::ref(cache),
                                std::ref(domain_queue), std::ref(stop_processing),
                                std::ref(reporter), std::ref(pool));

        // 启动抓包线程
        std::thread capture_thread(start_packet_capture, device);
        
        std::thread stats_thread(stats_processor, std::ref(cache), std::ref(stop_processing), std::ref(reporter), 60); // 每 60 秒上报

        while (!stop_processing.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // 等待处理线程结束
        domain_queue.push("");  // 确保处理线程能退出
        cache_thread.join();
        capture_thread.join();
        // stats_thread.join();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}