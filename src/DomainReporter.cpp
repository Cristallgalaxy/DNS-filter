#include "DomainReporter.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <ctime>
#include <thread>
#include <mutex>
#include <memory>
#include "RedisDNSCache.h"

DomainReporter& DomainReporter::getInstance() {
    static DomainReporter instance;
    return instance;
}

DomainReporter::DomainReporter() {
    serverUrl = "http://localhost:8080/hello"; // 默认 URL
}

DomainReporter::~DomainReporter() {
    if (curlInitialized.load()) {
        curl_slist_free_all(defaultHeaders);
        curl_global_cleanup();
    }
}

void DomainReporter::setServerUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(urlMutex);
    serverUrl = url;
}

void DomainReporter::initializeCurl() {
    if (!curlInitialized.load()) {
        curl_global_init(CURL_GLOBAL_ALL);
        defaultHeaders = curl_slist_append(nullptr, "Content-Type: application/json");
        defaultHeaders = curl_slist_append(defaultHeaders, "Accept: application/json");
        curlInitialized.store(true);
    }
}

size_t DomainReporter::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// 设置 CURL 请求参数
void DomainReporter::configureCurl(CURL* curl, const std::string& url, const std::string& json_data, std::string& response_data) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, defaultHeaders);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_data.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
}

bool DomainReporter::reportDomains(RedisDNSCache& cache, const std::vector<std::string>& domains) {
    if (domains.empty()) return true;

    try {
        initializeCurl();

        // 构造 JSON 数据
        nlohmann::json payload = {
            {"domains", domains},
            {"timestamp", std::time(nullptr)}
        };
        std::string json_data = payload.dump();

        // 初始化 CURL，RAII 管理
        struct CurlDeleter {
            void operator()(CURL* c) const { if (c) curl_easy_cleanup(c); }
        };
        std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
        if (!curl) {
            std::cerr << "[DomainReporter] Failed to initialize CURL" << std::endl;
            return false;
        }

        std::string response_data;
        {
            std::lock_guard<std::mutex> lock(urlMutex);
            configureCurl(curl.get(), serverUrl, json_data, response_data);
        }

        CURLcode res = curl_easy_perform(curl.get());
        if (res != CURLE_OK) {
            std::cerr << "[DomainReporter] CURL request failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }

        long response_code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);

        if (response_code < 200 || response_code >= 300) {
            std::cerr << "[DomainReporter] Server returned error code: " << response_code << std::endl;
            return false;
        }

        if(response_code >= 200 && response_code < 300) {
            // std::cout << "[DomainReporter] Report successful. HTTP " << response_code << std::endl;
            for (const auto& domain : domains) {
                std::shared_ptr<RedisDNSCache::DomainEntry> info = cache.find(domain);
                if (info && info->status == DomainStatus::FAKE) {
                    cache.insertOrUpdate(domain, DomainStatus::PEND, info->action);
                    // std::cout << "[DomainReporter] Marked domain as PEND: " << domain << std::endl;
                }
            }

            // 尝试解析响应
            try {
                nlohmann::json response_json = nlohmann::json::parse(response_data);

                auto handleArray = [&](const std::string& key, DomainAction action) {
                    if (response_json.contains(key) && response_json[key].is_array()) {
                        for (const auto& domain : response_json[key]) {
                            std::string d = domain.get<std::string>();
                            // std::cout << "[DomainReporter] " << (action == DomainAction::PERMIT ? "PERMIT" : "DROP") << ": " << d << std::endl;
                            cache.insertOrUpdate(d, DomainStatus::FULL, action);
                        }
                    }
                };

                handleArray("permitted", DomainAction::PERMIT);
                handleArray("dropped", DomainAction::DROP);

                
                return true;

            } catch (const std::exception& e) {
                std::cerr << "[DomainReporter] JSON parse error: " << e.what() << std::endl;
                return false;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[DomainReporter] Exception: " << e.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[DomainReporter] Unknown exception occurred" << std::endl;
        return false;
    }
}

void DomainReporter::try_report_domains(
    RedisDNSCache& cache,
    size_t max_retries,
    std::chrono::seconds retry_delay
) {
    size_t attempt = 0;
    bool success = false;
    std::vector<std::string> domains = cache.getPendingReportDomains();
    std::vector<std::string> filtered_domains;
    
    // 过滤待上报域名，跳过已经ttl超时的域名
    for(const std::string& domain:domains) {
        auto entry = cache.find(domain);
        if(entry) {
            filtered_domains.push_back(domain);
        } 
    }

    if (filtered_domains.empty()) {
        std::cout << "[DomainReporter] No valid domains to report\n";
        cache.clearPendingReportDomains();  // 清空 set
    }

    // 上报逻辑 + 重试机制
    while (attempt < max_retries && !success) {
        // std::cout << "[DomainReporter] Reporting " << filtered_domains.size() 
        //           << " domains (attempt " << (attempt + 1) << ")\n";
        success = reportDomains(cache, filtered_domains);  // 用 curl 发请求

        if (!success) {
            ++attempt;
            if (attempt < max_retries) {
                std::this_thread::sleep_for(retry_delay);
            }
        }
    }

    if (success) {
        cache.clearPendingReportDomains();  // 成功上报后清空 set
    } else {
        std::cerr << "[DomainReporter] Failed after " << max_retries << " attempts\n";
    }

}

void DomainReporter::reportStats(RedisDNSCache& cache, int interval_seconds) {
    std::vector<nlohmann::json> report_data;

    auto all = cache.getAllDomainData();

    for (const auto& [domain, meta] : all) {
        if (meta.query_count > 0) {
            report_data.push_back({
                {"domain", domain},
                {"action", meta.action == DomainAction::DROP ? "DROP" : "PERMIT"},
                {"queries", meta.query_count}
            });
        }
    }

    // 构造 JSON
    nlohmann::json payload = {
        {"stats", report_data},
        {"timestamp", std::time(nullptr)}
    };

    std::string json_data = payload.dump();
    // std::cout << "[DomainReporter] Reporting stats payload:\n" << json_data << "\n";

    // curl 上报
    try {
        CURL* curl = curl_easy_init();

        if (curl) {
            std::string response_data;
            {
                std::lock_guard<std::mutex> lock(urlMutex);
                configureCurl(curl, serverUrl, json_data, response_data);
            }

            CURLcode res = curl_easy_perform(curl);

            if (res == CURLE_OK) {
                long response_code;

                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

                if (response_code >= 200 && response_code < 300) {
                    std::cout << "[DomainReporter] Stats reported successfully\n";
                    // 清零访问次数
                    for (const auto& entry : report_data) {
                        cache.resetQueryCount(entry["domain"].get<std::string>());
                    }
                } else {
                    std::cerr << "[DomainReporter] Stats report failed, HTTP " << response_code << "\n";
                }
            } else {
                std::cerr << "[DomainReporter] CURL failed: " << curl_easy_strerror(res) << "\n";
            }
            curl_easy_cleanup(curl);
        }
    } catch (...) {
        std::cerr << "[DomainReporter] Exception while reporting stats\n";
    }
}

