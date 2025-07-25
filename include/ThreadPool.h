// ThreadPool.h
#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

/**
 * @brief 线程池类，实现固定数量的工作线程和任务队列
 * 
 * 线程池维护一个固定大小的线程组，每个线程不断从任务队列取任务执行，
 * 支持向线程池提交任意可调用对象，返回对应的 std::future 以获取结果。
 * 
 * 通过线程池可以高效复用线程资源，减少频繁创建销毁线程的开销。
 */
class ThreadPool {
public:
    /**
     * @brief 构造函数，启动指定数量的工作线程
     * @param thread_count 线程池中线程的数量
     */
    explicit ThreadPool(size_t thread_count);

    /**
     * @brief 析构函数，停止所有线程并释放资源
     * 会等待所有任务执行完成后再退出
     */
    ~ThreadPool();

    /**
     * @brief 向线程池提交任务
     * 
     * 任务可以是任意可调用对象（函数、lambda、函数对象等），
     * 支持传入参数，并返回对应任务的 future 用于获取执行结果。
     * 
     * @tparam F 任务类型（可调用对象）
     * @tparam Args 任务参数类型
     * @param f 任务函数或可调用对象
     * @param args 传递给任务函数的参数
     * @return std::future<任务返回类型> 任务执行结果的 future
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result_t<F, Args...>>;

private:
    std::vector<std::thread> workers;                ///< 工作线程容器
    std::queue<std::function<void()>> tasks;         ///< 任务队列

    std::mutex queue_mutex;                           ///< 任务队列互斥锁
    std::condition_variable condition;                ///< 任务队列条件变量，用于线程等待唤醒
    std::atomic<bool> stop;                           ///< 停止标志，通知线程池停止工作
};

#include "ThreadPool.tpp"  // 模板函数实现
