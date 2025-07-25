// ThreadSafeQueue.h
#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H
#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

/**
 * @brief 线程安全队列模板类
 * 
 * 该类实现了一个线程安全的队列，支持多个线程并发地进行入队和出队操作。
 * 使用互斥锁保护队列的读写，并使用条件变量实现阻塞等待。
 * 
 * @tparam T 队列中存储的元素类型
 */
template <typename T>
class ThreadSafeQueue {
public:
    /**
     * @brief 向队列尾部添加一个元素
     * 
     * 该方法线程安全，添加元素后会通知一个等待线程。
     * 
     * @param value 要添加的元素
     */
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
        cond_var_.notify_one(); // Notify one waiting thread
    }

    /**
     * @brief 尝试从队列头部取出一个元素（非阻塞）
     * 
     * 如果队列为空，立即返回 false。
     * 
     * @param value 输出参数，成功时存储弹出的元素
     * @return true 取出元素成功
     * @return false 队列为空，未能取出元素
     */
    bool try_pop(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

    /**
     * @brief 阻塞直到成功从队列头部取出一个元素
     * 
     * 如果队列为空，会阻塞等待直到有元素可用。
     * 
     * @param value 输出参数，存储弹出的元素
     */
    void wait_and_pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_var_.wait(lock, [this] { return !queue_.empty(); }); // Wait until queue not empty
        value = queue_.front();
        queue_.pop();
    }

    /**
     * @brief 判断队列是否为空
     * 
     * @return true 队列为空
     * @return false 队列不为空
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;           // 互斥锁，保护队列的线程安全
    std::queue<T> queue_;                // 内部存储的标准队列
    std::condition_variable cond_var_;  // 条件变量，用于阻塞等待
};

#endif // THREAD_SAFE_QUEUE_H
