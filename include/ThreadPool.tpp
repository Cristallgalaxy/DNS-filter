// ThreadPool.tpp
#pragma once

#include "ThreadPool.h"

/**
 * @brief 构造函数，启动指定数量的工作线程
 * 
 * 每个线程在一个循环中等待任务队列条件变量的通知，
 * 线程池停止或任务队列非空时线程唤醒，取出任务执行。
 * 
 * @param threads 线程池中线程数量
 */
inline ThreadPool::ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            while (!stop) {
                std::function<void()> task;

                {
                    // 获取任务队列锁，等待条件变量通知
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    condition.wait(lock, [this] {
                        return stop.load() || !tasks.empty();
                    });

                    // 停止且任务队列空，线程退出
                    if (stop && tasks.empty()) return;

                    // 从任务队列取出任务
                    task = std::move(tasks.front());
                    tasks.pop();
                }

                // 执行任务
                task();
            }
        });
    }
}

/**
 * @brief 析构函数，停止线程池并等待所有线程退出
 * 
 * 设置停止标志，通知所有等待的线程，
 * 并 join 所有工作线程，确保安全退出。
 */
inline ThreadPool::~ThreadPool() {
    stop = true;
    condition.notify_all();
    for (std::thread& worker : workers)
        if (worker.joinable()) worker.join();
}

/**
 * @brief 向线程池提交新任务
 * 
 * 将任务封装为 std::packaged_task 放入任务队列，
 * 并通知一个等待线程执行任务。
 * 返回 std::future 以供调用方获取任务结果。
 * 
 * @tparam F 任务可调用对象类型
 * @tparam Args 任务参数类型包
 * @param f 任务函数或可调用对象
 * @param args 传递给任务的参数
 * @return std::future<任务返回类型> 任务执行结果的 future
 */
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result_t<F, Args...>> {
    using return_type = typename std::invoke_result_t<F, Args...>;

    // 创建共享的打包任务
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    {
        // 将任务添加到任务队列
        std::lock_guard<std::mutex> lock(queue_mutex);
        tasks.emplace([task]() { (*task)(); });
    }

    // 通知一个线程任务已准备好
    condition.notify_one();

    // 返回任务的 future
    return task->get_future();
}
