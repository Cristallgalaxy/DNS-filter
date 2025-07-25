#ifndef REDISDNS_CACHE_H
#define REDISDNS_CACHE_H
#pragma once

#include <hiredis/hiredis.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <map>
#include <vector>
#include <unordered_map>

// Redis 连接配置
constexpr const char* REDIS_HOST = "127.0.0.1";       // Redis 服务地址
constexpr int REDIS_PORT = 6379;                      // Redis 服务端口
constexpr int REDIS_TIMEOUT_SEC = 1;                  // 连接超时（秒）
constexpr int REDIS_TIMEOUT_USEC = 500000;            // 连接超时（微秒）
constexpr const char* REDIS_PASSWORD = "asdfgh123-";  // Redis 认证密码（若有）

// 待上报域名集合 Redis Key
constexpr const char* REDIS_PENDING_REPORT_SET = "pending_report_domains";

// 域名状态（用于标记当前域名的识别处理阶段）
enum class DomainStatus {
    FAKE,  // 未识别：还未上报服务器
    PEND,  // 待判定：已上报服务器，正在分析中
    FULL   // 完全识别：服务器已回复，状态已明确
};

// 域名动作（表示对该域名的处理策略）
enum class DomainAction {
    DROP,   // 拒绝访问（阻断 DNS 响应）
    PERMIT  // 允许访问（放行 DNS 响应）
};

/**
 * @brief RedisDNSCache 类：用于缓存域名状态和处理信息到 Redis，并提供 TTL、LRU 控制与统计功能
 */
class RedisDNSCache {
public:
    /**
     * @brief 域名缓存条目结构
     */
    struct DomainEntry {
        std::string domain;          // 域名字符串
        DomainStatus status;         // 当前状态（FAKE/PEND/FULL）
        DomainAction action;         // 当前动作（DROP/PERMIT）
        uint32_t query_count;        // 访问次数计数器
        time_t last_updated;         // 状态最后更新时间
        time_t last_accessed;        // 最后一次访问时间（用于 LRU 策略）
    };

    struct DomainMeta {
        DomainStatus status;
        DomainAction action;
        uint32_t query_count;
    };

    // 构造函数：可以指定最大缓存容量（默认10000条）
    explicit RedisDNSCache(size_t max_size = 10);

    // 析构函数：释放 Redis 连接资源
    ~RedisDNSCache();

    // 设置各类条目的 TTL（秒）
    void setTTLConfig(int fake, int pend, int full_permit, int full_drop);

    // 插入一个新域名条目（若已存在则失败）
    bool insert(const std::string& domain, DomainStatus status, DomainAction action);

    // 更新已有条目的状态/动作（通过现有 shared_ptr 引用）
    bool update(const std::shared_ptr<DomainEntry>& existing_entry,
                const std::string& domain, DomainStatus status, DomainAction action);

    // 插入或更新：存在则更新，不存在则插入
    bool insertOrUpdate(const std::string& domain, DomainStatus status, DomainAction action);

    // 查找某个域名的缓存条目（命中返回 shared_ptr，否则返回 nullptr）
    std::shared_ptr<DomainEntry> find(const std::string& domain);

    // 删除某个域名条目（从 Redis 和本地缓存中移除）
    bool remove(const std::string& domain);

    // 执行清理策略（LRU 淘汰或 TTL 过期清除）
    void cleanup();

    // 获取当前缓存条目的数量
    size_t size();

    // 打印所有条目信息（调试用途）
    void printAllData();

    //将新插入域名添加到待上报集合
    void addToPendingReportSet(const std::string& domain);

    //获取待上报域名集合中的所有域名列表
    std::vector<std::string> getPendingReportDomains();

    //获取待上报域名集合的元素数量
    int getPendingReportCount();

    //清空待上报域名集合
    void clearPendingReportDomains();

    // 获取所有域名的简要信息（供上报模块使用）
    // 返回：unordered_map，其中 key 为域名，value 为状态、动作和查询次数
    std::unordered_map<std::string, DomainMeta> getAllDomainData();

    // 重置某个域名的查询次数为 0（例如在成功上报后）
    void resetQueryCount(const std::string& domain);




private:
    /**
     * @brief 将状态枚举值转换为字符串（用于 Redis 存储或打印）
     */
    static std::string statusToString(DomainStatus status);

    /**
     * @brief 将动作枚举值转换为字符串（用于 Redis 存储或打印）
     */
    static std::string actionToString(DomainAction action);

    /**
     * @brief TTL 配置结构体，指定不同状态下的生存时间（单位：秒）
     */
    struct TTLConfig {
        int fake = 300;           // FAKE 状态的默认 TTL（5 分钟）
        int pend = 600;           // PEND 状态的默认 TTL（10 分钟）
        int full_permit = 86400;  // FULL+PERMIT 的默认 TTL（1 天）
        int full_drop = 3600;     // FULL+DROP 的默认 TTL（1 小时）
    };

    // redisContext* redis_conn;  // hiredis Redis 连接对象
    std::mutex mtx;            // 线程安全互斥锁
    size_t max_size;           // 缓存容量上限（触发 LRU）
    TTLConfig ttl_config;      // 当前 TTL 设置参数

    /**
     * @brief 获取或初始化 Redis 连接（懒加载）
     */
    redisContext* getConnection();

    /**
     * @brief 执行 Redis 写命令（格式化参数，无需返回值）
     */
    void executeCommand(const char* format, ...);

    /**
     * @brief 执行 Redis 查询命令，并返回字符串类型结果
     */
    std::string getStringResult(const char* format, ...);

    /**
     * @brief 根据条目的状态与动作类型，确定应设置的 TTL 值（秒）
     */
    int getTTL(DomainStatus status, DomainAction action);

    /**
     * @brief 预检查并确保缓存容量足够，若超过上限则触发 LRU 淘汰
     */
    void makeRoom();

    /**
     * @brief 清除 Redis 中过期的域名条目（依赖 TTL 和 last_updated 时间）
     */
    void cleanupExpired();
};

// ------------------ 内联辅助函数定义 ------------------

inline std::string RedisDNSCache::statusToString(DomainStatus status) {
    switch(status) {
        case DomainStatus::FAKE: return "FAKE";
        case DomainStatus::PEND: return "PEND";
        case DomainStatus::FULL: return "FULL";
        default: return "UNKNOWN";
    }
}

inline std::string RedisDNSCache::actionToString(DomainAction action) {
    switch(action) {
        case DomainAction::DROP: return "DROP";
        case DomainAction::PERMIT: return "PERMIT";
        default: return "UNKNOWN";
    }
}

#endif // REDISDNS_CACHE_H
