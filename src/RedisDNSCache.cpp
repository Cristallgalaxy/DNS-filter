#include <iostream>
#include <vector>
#include <chrono>
#include <stdexcept>
#include <atomic>
#include <iomanip>
#include <stdexcept>
#include <cstdarg>
#include <optional>
#include <unordered_map>

#include "RedisDNSCache.h"

RedisDNSCache::RedisDNSCache(size_t max_size) 
    : max_size(max_size) {
}

RedisDNSCache::~RedisDNSCache() {

}

redisContext* RedisDNSCache::getConnection() {
    thread_local redisContext* conn = nullptr;

    if (!conn || conn->err) {
        if (conn) {
            std::cerr << "[Redis] Closing broken connection: "
                      << (conn->errstr ? conn->errstr : "Unknown error") << std::endl;
            redisFree(conn);
            conn = nullptr;
        }

        const struct timeval timeout = { REDIS_TIMEOUT_SEC, REDIS_TIMEOUT_USEC };
        conn = redisConnectWithTimeout(REDIS_HOST, REDIS_PORT, timeout);

        if (!conn) {
            throw std::runtime_error("[getConnection] Redis connection allocation failed");
        }

        if (conn->err) {
            std::string err_msg = "[getConnection] Redis connection failed: ";
            err_msg += conn->errstr ? conn->errstr : "Unknown error";
            redisFree(conn);
            conn = nullptr;
            throw std::runtime_error(err_msg);
        }

        // Redis AUTH
        std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
            (redisReply*)redisCommand(conn, "AUTH %s", REDIS_PASSWORD),
            freeReplyObject
        );

        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            std::string err_msg = "[getConnection] Authentication failed: ";
            err_msg += (reply && reply->str) ? reply->str : "No error message";
            redisFree(conn);
            conn = nullptr;
            throw std::runtime_error(err_msg);
        }

        std::cout << "[Redis] Connection authenticated successfully (thread-local)" << std::endl;
    }

    return conn;
}

void RedisDNSCache::executeCommand(const char* format, ...) {
    va_list ap;
    va_start(ap, format);

    redisContext* conn = getConnection();
    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        (redisReply*)redisvCommand(conn, format, ap), freeReplyObject
    );
    va_end(ap);

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        std::string err = (reply && reply->str) ? reply->str : "unknown error";
        throw std::runtime_error("Redis executeCommand error: " + err);
    }
}

std::string RedisDNSCache::getStringResult(const char* format, ...) {
    va_list ap;
    va_start(ap, format);

    redisContext* conn = getConnection();
    std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        (redisReply*)redisvCommand(conn, format, ap), freeReplyObject
    );
    va_end(ap);

    if (!reply || reply->type == REDIS_REPLY_ERROR) {
        std::string err = (reply && reply->str) ? reply->str : "unknown error";
        throw std::runtime_error("Redis getStringResult error: " + err);
    }

    if (reply->type != REDIS_REPLY_STRING) {
        throw std::runtime_error("Redis getStringResult: unexpected reply type");
    }

    return std::string(reply->str, reply->len);
}


int RedisDNSCache::getTTL(DomainStatus status, DomainAction action) {
    switch(status) {
        case DomainStatus::FAKE: return ttl_config.fake;
        case DomainStatus::PEND: return ttl_config.pend;
        case DomainStatus::FULL: 
            return (action == DomainAction::PERMIT) ? ttl_config.full_permit : ttl_config.full_drop;
    }
    return ttl_config.full_permit;
}

void RedisDNSCache::makeRoom() {
    long long current_size = 0;
    redisReply* reply = (redisReply*)redisCommand(getConnection(), "ZCARD dns:lru");
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        current_size = reply->integer;
    }
    freeReplyObject(reply);

    if (current_size >= max_size) {
        int remove_count = std::max(2, static_cast<int>(max_size * 0.1));

        const char* lua_script = 
            "local keys = redis.call('ZRANGE', 'dns:lru', 0, ARGV[1])\n"
            "for i, key in ipairs(keys) do\n"
            "  redis.call('DEL', 'dns:entries:'..key)\n"
            "  redis.call('ZREM', 'dns:lru', key)\n"
            "end\n"
            "return #keys";

        redisReply* lua_reply = (redisReply*)redisCommand(getConnection(),
            "EVAL %s 0 %d", lua_script, remove_count - 1);
        freeReplyObject(lua_reply);
    }
}


void RedisDNSCache::cleanupExpired() {
    
    long long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    const char* lua_script = 
        "local keys = redis.call('ZRANGE', 'dns:lru', 0, -1)\n"
        "for i, key in ipairs(keys) do\n"
        "    local ttl = redis.call('HGET', 'dns:entries:'..key, 'ttl')\n"
        "    local updated = redis.call('HGET', 'dns:entries:'..key, 'last_updated')\n"
        "    if ttl and updated and tonumber(updated) + tonumber(ttl) < tonumber(ARGV[1]) then\n"
        "        redis.call('DEL', 'dns:entries:'..key)\n"
        "        redis.call('ZREM', 'dns:lru', key)\n"
        "    end\n"
        "end\n"
        "return #keys";
    
    // 释放锁后再执行Lua脚本
    redisReply* reply = (redisReply*)redisCommand(getConnection(), 
        "EVAL %s 0 %lld", lua_script, now);
    freeReplyObject(reply);
}

void RedisDNSCache::printAllData() {
    try {
        std::cout << "\n=== Current Redis DNS Cache Contents ===\n";

        // 获取所有域名键（按照 LRU 排序）
        redisReply* keys_reply = (redisReply*)redisCommand(getConnection(), "ZRANGE dns:lru 0 -1");
        if (!keys_reply || keys_reply->type != REDIS_REPLY_ARRAY) {
            std::cerr << "Failed to get keys from dns:lru" << std::endl;
            if (keys_reply) freeReplyObject(keys_reply);
            return;
        }

        // 打印主缓存表头
        std::cout << std::left << std::setw(50) << "Domain"
                  << std::setw(10) << "Status"
                  << std::setw(10) << "Action"
                  << std::setw(10) << "Queries"
                  << std::setw(20) << "Last Updated"
                  << std::setw(20) << "Last Accessed"
                  << std::setw(10) << "TTL"
                  << std::endl;
        std::cout << std::string(130, '-') << std::endl;

        // 遍历所有缓存条目
        for (size_t i = 0; i < keys_reply->elements; i++) {
            std::string domain(keys_reply->element[i]->str, keys_reply->element[i]->len);

            // 获取域名详细字段
            redisReply* entry_reply = (redisReply*)redisCommand(
                getConnection(), "HGETALL dns:entries:%s", domain.c_str());

            if (entry_reply && entry_reply->type == REDIS_REPLY_ARRAY && entry_reply->elements > 0) {
                std::map<std::string, std::string> fields;
                for (size_t j = 0; j < entry_reply->elements; j += 2) {
                    std::string key(entry_reply->element[j]->str, entry_reply->element[j]->len);
                    std::string value(entry_reply->element[j + 1]->str, entry_reply->element[j + 1]->len);
                    fields[key] = value;
                }

                // 打印每个域名条目
                std::cout << std::left << std::setw(50) << domain
                          << std::setw(10) << statusToString(static_cast<DomainStatus>(std::stoi(fields["status"])))
                          << std::setw(10) << actionToString(static_cast<DomainAction>(std::stoi(fields["action"])))
                          << std::setw(10) << fields["query_count"]
                          << std::setw(20) << fields["last_updated"]
                          << std::setw(20) << fields["last_accessed"]
                          << std::setw(10) << fields["ttl"]
                          << std::endl;
            }

            if (entry_reply) freeReplyObject(entry_reply);
        }

        std::cout << "=== Total entries: " << keys_reply->elements << " ===\n\n";
        freeReplyObject(keys_reply);

        // 打印待上报集合 dns:pending_set
        std::cout << "\n=== Domains in Pending Report Set (dns:pending_set) ===\n";
        redisReply* set_reply = (redisReply*)redisCommand(getConnection(), "SMEMBERS %s", REDIS_PENDING_REPORT_SET);
        if (set_reply && set_reply->type == REDIS_REPLY_ARRAY) {
            if (set_reply->elements == 0) {
                std::cout << "(empty set)\n";
            } else {
                for (size_t i = 0; i < set_reply->elements; ++i) {
                    std::string pending_domain(set_reply->element[i]->str, set_reply->element[i]->len);
                    std::cout << "- " << pending_domain << std::endl;
                }
            }
        } else {
            std::cerr << "Failed to get members from dns:pending_set\n";
        }
        std::cout << "===Total entries: " << this->getPendingReportCount() << " ===\n\n";

        if (set_reply) freeReplyObject(set_reply);

    } catch (const std::exception& e) {
        std::cerr << "Error printing Redis data: " << e.what() << std::endl;
    }
}

void RedisDNSCache::addToPendingReportSet(const std::string& domain) {
    std::lock_guard<std::mutex> lock(mtx);
    redisContext* conn = getConnection();
    if (!conn) {
        throw std::runtime_error("Redis connection failed");
    }
    redisReply* reply = (redisReply*)redisCommand(conn, "SADD %s %s", REDIS_PENDING_REPORT_SET, domain.c_str());
    if (!reply) {
        throw std::runtime_error("Failed to execute SADD command");
    }
    freeReplyObject(reply);
}

std::vector<std::string> RedisDNSCache::getPendingReportDomains() {
    std::lock_guard<std::mutex> lock(mtx);
    std::vector<std::string> domains;
    redisContext* conn = getConnection();
    if (!conn) {
        throw std::runtime_error("Redis connection failed");
    }

    redisReply* reply = (redisReply*)redisCommand(conn, "SMEMBERS %s", REDIS_PENDING_REPORT_SET);
    if (!reply) {
        throw std::runtime_error("Failed to execute SMEMBERS command");
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            domains.emplace_back(reply->element[i]->str, reply->element[i]->len);
        }
    }
    freeReplyObject(reply);
    return domains;
}

int RedisDNSCache::getPendingReportCount() {
    std::lock_guard<std::mutex> lock(mtx);
    redisContext* conn = getConnection();
    if (!conn) {
        throw std::runtime_error("Redis connection failed");
    }

    redisReply* reply = (redisReply*)redisCommand(conn, "SCARD %s", REDIS_PENDING_REPORT_SET);
    // if (!reply) {
    //     throw std::runtime_error("Failed to execute SCARD command");
    // }

    int count = 0;
    if (reply->type == REDIS_REPLY_INTEGER) {
        count = static_cast<int>(reply->integer);
    }
    freeReplyObject(reply);
    return count;
}

void RedisDNSCache::clearPendingReportDomains() {
    std::lock_guard<std::mutex> lock(mtx);
    redisContext* conn = getConnection();
    if (!conn) {
        throw std::runtime_error("Redis connection failed");
    }
    redisReply* reply = (redisReply*)redisCommand(conn, "DEL %s", REDIS_PENDING_REPORT_SET);
    if (!reply) {
        throw std::runtime_error("Failed to execute DEL command");
    }
    freeReplyObject(reply);
}

std::unordered_map<std::string, RedisDNSCache::DomainMeta> RedisDNSCache::getAllDomainData() {
    std::unordered_map<std::string, RedisDNSCache::DomainMeta> result;

    redisReply* reply = (redisReply*)redisCommand(getConnection(), "ZRANGE dns:lru 0 -1");
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        // throw std::runtime_error("Failed to fetch domain keys from dns:lru");
        // return std::nullopt;
    }

    for (size_t i = 0; i < reply->elements; ++i) {
        std::string domain = reply->element[i]->str;
        std::string hash_key = "dns:entries:" + domain;

        redisReply* data = (redisReply*)redisCommand(getConnection(),
            "HMGET %s status action query_count", hash_key.c_str());

        if (!data || data->type != REDIS_REPLY_ARRAY || data->elements < 3) {
            if (data) freeReplyObject(data);
            continue;
        }

        DomainMeta meta;
        if (data->element[0]->str)
            meta.status = static_cast<DomainStatus>(std::stoi(data->element[0]->str));
        if (data->element[1]->str)
            meta.action = static_cast<DomainAction>(std::stoi(data->element[1]->str));
        if (data->element[2]->str)
            meta.query_count = std::stoul(data->element[2]->str);
        else
            meta.query_count = 0;

        result[domain] = meta;

        freeReplyObject(data);
    }

    freeReplyObject(reply);
    return result;
}

void RedisDNSCache::resetQueryCount(const std::string& domain) {
    std::lock_guard<std::mutex> lock(mtx);
    std::string hash_key = "dns:entries:" + domain;

    redisReply* reply = (redisReply*)redisCommand(getConnection(),
        "HSET %s query_count 0", hash_key.c_str());

    if (!reply || (reply->type == REDIS_REPLY_ERROR)) {
        if (reply) freeReplyObject(reply);
        std::cerr << "[RedisDNS] Failed to reset query_count for domain: " << domain << "\n";
        return;
    }

    // std::cout << "[RedisDNS] Reset query_count for domain: " << domain << "\n";
    freeReplyObject(reply);
}

bool RedisDNSCache::insert(const std::string& domain, DomainStatus status, DomainAction action) {
    try {
        makeRoom();
        
        long long now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int ttl = getTTL(status, action);

        std::cout << "[RedisDNS] Adding domain: " << domain
                  << " (Status: " << statusToString(status)
                  << ", Action: " << actionToString(action)
                  << ", TTL: " << ttl << "s)" << std::endl;

        executeCommand(
            "HMSET dns:entries:%s domain %s status %d action %d "
            "last_updated %lld last_accessed %lld query_count %d ttl %d",
            domain.c_str(), domain.c_str(), static_cast<int>(status),
            static_cast<int>(action), now, now, 1, ttl);

        executeCommand("ZADD dns:lru %lld %s", now, domain.c_str());
        addToPendingReportSet(domain);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Redis insert error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisDNSCache::update(const std::shared_ptr<DomainEntry>& existing_entry, const std::string& domain, DomainStatus status, DomainAction action) {
    try {
        long long now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int ttl = getTTL(status, action);

        std::cout << "[RedisDNS] Updating domain: " << domain
                  << " (Status: " << statusToString(status)
                  << ", Action: " << actionToString(action)
                  << ", TTL: " << ttl << "s)" << std::endl;

        uint32_t new_query_count = (existing_entry->status == status)
            ? existing_entry->query_count + 1
            : existing_entry->query_count;

        executeCommand(
            "HMSET dns:entries:%s domain %s status %d action %d "
            "last_updated %lld last_accessed %lld query_count %d ttl %d",
            domain.c_str(), domain.c_str(), static_cast<int>(status),
            static_cast<int>(action), now, now, new_query_count, ttl);

        executeCommand("ZADD dns:lru %lld %s", now, domain.c_str());

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Redis update error: " << e.what() << std::endl;
        return false;
    }
}

bool RedisDNSCache::insertOrUpdate(const std::string& domain, DomainStatus status, DomainAction action) {
    cleanupExpired();
    std::shared_ptr<DomainEntry> existing_entry = find(domain);
    if (existing_entry == nullptr) {
        return insert(domain, status, action);
    } else {
        return update(existing_entry, domain, status, action);
    }
}

std::shared_ptr<RedisDNSCache::DomainEntry> RedisDNSCache::find(const std::string& domain) {
    try {
        // 查询 Redis 中是否存在指定域名的哈希表
        redisReply* reply = (redisReply*)redisCommand(getConnection(),
            "HGETALL dns:entries:%s", domain.c_str());

        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR || reply->elements == 0) {
            freeReplyObject(reply);
            return nullptr;
        }

        DomainEntry entry;
        std::map<std::string, std::string> fields;

        // 将所有字段存入 map
        for (size_t i = 0; i < reply->elements; i += 2) {
            std::string key(reply->element[i]->str, reply->element[i]->len);
            std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);
            fields[key] = value;
        }
        freeReplyObject(reply);

        // 解析字段构造对象
        entry.domain = fields["domain"];
        entry.status = static_cast<DomainStatus>(std::stoi(fields["status"]));
        entry.action = static_cast<DomainAction>(std::stoi(fields["action"]));
        entry.query_count = std::stoul(fields["query_count"]);
        entry.last_updated = std::stoll(fields["last_updated"]);
        entry.last_accessed = std::stoll(fields["last_accessed"]);

        return std::make_shared<DomainEntry>(entry);
    } catch (const std::exception& e) {
        std::cerr << "Redis error: " << e.what() << std::endl;
        return nullptr;
    }
}

bool RedisDNSCache::remove(const std::string& domain) {
    std::unique_lock<std::mutex> lock(mtx);
    try {
        std::cout << "[RedisDNS] Removing domain: " << domain << std::endl;
        
        lock.unlock();
        executeCommand("DEL dns:entries:%s", domain.c_str());
        executeCommand("ZREM dns:lru %s", domain.c_str());
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Redis error: " << e.what() << std::endl;
        return false;
    }
}

// void RedisDNSCache::cleanup() {
//     std::lock_guard<std::mutex> lock(mtx);
//     cleanupExpired();
// }

size_t RedisDNSCache::size() {
    std::lock_guard<std::mutex> lock(mtx);
    
    redisReply* reply = (redisReply*)redisCommand(getConnection(), "ZCARD dns:lru");
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
        freeReplyObject(reply);
        throw std::runtime_error("Failed to get cache size");
    }
    
    size_t count = reply->integer;
    freeReplyObject(reply);
    return count;
}