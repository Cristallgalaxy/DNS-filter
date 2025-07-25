#ifndef DOMAIN_REPORTER_H
#define DOMAIN_REPORTER_H
#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <curl/curl.h>
#include "RedisDNSCache.h"  // 包含 RedisDNSCache 的定义，用于访问 DNS 缓存

/**
 * @brief DomainReporter 是一个单例类，用于将 RedisDNSCache 中的域名信息上报到指定 HTTP 服务器。
 *
 * 功能包括：
 * - 设置上报服务器地址
 * - 执行域名上报操作
 * - 提供带重试机制的域名上报
 * 
 * 上报数据通过 HTTP POST 以 JSON 格式发送，使用 libcurl 实现。
 * 
 * 线程安全：使用 std::mutex 保护共享资源，curl 初始化仅执行一次。
 */
class DomainReporter {
public:
    // 删除复制构造函数和赋值运算符，确保该类只能通过 getInstance 获取单例
    DomainReporter(const DomainReporter&) = delete;
    DomainReporter& operator=(const DomainReporter&) = delete;

    /**
     * @brief 获取 DomainReporter 的唯一实例（单例模式）
     * @return DomainReporter& 引用类型的唯一实例
     */
    static DomainReporter& getInstance();

    /**
     * @brief 设置远程上报服务器的 URL（例如：http://127.0.0.1:8080/hello）
     * @param url 要上报到的服务器地址（HTTP POST）
     */
    void setServerUrl(const std::string& url);

    /**
     * @brief 向远程服务器上报指定的域名列表
     * 
     * 该函数会将域名及其状态从 RedisDNSCache 中提取并序列化为 JSON，
     * 然后通过 HTTP POST 请求发送到 serverUrl 指定的服务器。
     *
     * @param cache RedisDNSCache 实例，用于获取域名状态等信息
     * @param domains 需要上报的域名列表
     * @return true 表示上报成功
     * @return false 表示上报失败
     */
    bool reportDomains(RedisDNSCache& cache, const std::vector<std::string>& domains);

    /**
     * @brief 尝试上报域名列表，失败后重试指定次数
     *
     * @param cache RedisDNSCache 实例
     * @param max_retries 最大重试次数（例如：3）
     * @param retry_delay 每次重试之间的时间间隔（例如：std::chrono::seconds(5)）
     */
    void try_report_domains(
        RedisDNSCache& cache,
        size_t max_retries,
        std::chrono::seconds retry_delay
    );

    /**
     * @brief 定期上报统计信息（例如每 N 秒一次）
     *
     * @param cache RedisDNSCache 实例
     * @param interval_seconds 上报间隔（单位：秒）
     */
    void reportStats(RedisDNSCache& cache, int interval_seconds);

private:
    /**
     * @brief 私有构造函数，仅供 getInstance 使用
     */
    DomainReporter();

    /**
     * @brief 私有析构函数，释放 curl 相关资源
     */
    ~DomainReporter();

    std::string serverUrl;               ///< 上报服务器地址
    std::atomic<bool> curlInitialized;   ///< 标记 curl 是否初始化（线程安全）
    std::mutex urlMutex;                 ///< 用于保护 serverUrl 的读写互斥
    struct curl_slist* defaultHeaders;   ///< 默认请求头，如 Content-Type: application/json

    /**
     * @brief 初始化 curl 全局资源，仅执行一次
     */
    void initializeCurl();

    /**
     * @brief 配置并执行 curl 请求
     * 
     * @param curl CURL 对象
     * @param url POST 请求目标 URL
     * @param json_data 发送的 JSON 字符串
     * @param response_data 接收服务器返回的数据
     */
    void configureCurl(CURL* curl, const std::string& url, const std::string& json_data, std::string& response_data);

    /**
     * @brief curl 写回调，用于获取响应内容
     * 
     * @param contents 返回内容指针
     * @param size 单个数据块大小
     * @param nmemb 数据块个数
     * @param userp 用户自定义指针（用于累积写入）
     * @return 实际写入的大小（size * nmemb）
     */
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

#endif // DOMAIN_REPORTER_H
