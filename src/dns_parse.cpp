#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <iostream>

#include "dns_parse.h"

std::optional<std::string> read_domain_name(const std::vector<uint8_t>& packet, size_t& pos, size_t max_len, int jump_limit) {
    std::string domain;
    int jumps = 0;
    size_t curr_pos = pos;

    while (curr_pos < max_len && jumps < jump_limit) {
        uint8_t len = packet[curr_pos];

        // 检测是否是压缩指针（以 11 开头的 2 字节）
        if ((len & 0xC0) == 0xC0) {
            if (curr_pos + 1 >= max_len) {
                std::cerr << "[dns_parse]: pointer overflow, missing second byte of compression pointer\n";
                return std::nullopt;
            }

            // 获取跳转的偏移量
            size_t offset = ((len & 0x3F) << 8) | packet[curr_pos + 1];
            if (offset >= max_len) {
                std::cerr << "[dns_parse]: pointer offset out of bounds, offset=" << offset << "\n";
                return std::nullopt;
            }

            // 递归读取跳转位置的域名
            size_t tmp_pos = offset;
            auto pointed = read_domain_name(packet, tmp_pos, max_len, jump_limit - 1);
            if (!pointed.has_value()) {
                std::cerr << "[dns_parse]: failed to parse domain name at compression pointer target\n";
                return std::nullopt;
            }

            // 拼接子域名
            if (!domain.empty() && !pointed->empty())
                domain += ".";
            domain += *pointed;

            curr_pos += 2; // 跳过指针两个字节
            break; // 压缩指针终止后续解析
        }

        // 正常结束（零长度 label）
        if (len == 0) {
            curr_pos++;
            break;
        }

        // 长度非法（应小于64）
        if (len > 63) {
            std::cerr << "[dns_parse]: invalid label length, len=" << (int)len << "\n";
            return std::nullopt;
        }

        // 检查是否越界
        if (curr_pos + len >= max_len) {
            std::cerr << "[dns_parse]: label length out of bounds, pos=" << curr_pos << ", len=" << (int)len << "\n";
            return std::nullopt;
        }

        // 提取当前 label，并拼接成 FQDN 形式
        if (!domain.empty()) domain += ".";
        domain += std::string(reinterpret_cast<const char*>(&packet[curr_pos + 1]), len);

        curr_pos += len + 1;
    }

    // 防止压缩指针死循环
    if (jumps >= jump_limit) {
        std::cerr << "[dns_parse]: exceeded jump limit, possible compression loop\n";
        return std::nullopt;
    }

    pos = curr_pos; // 返回时更新调用者的 pos
    return domain;
}

// 提取 DNS 查询部分中的所有域名
// 参数：packet 是完整的 DNS 数据包
std::vector<std::string> extract_dns_queries(const std::vector<uint8_t>& packet) {
    std::vector<std::string> domains;

    // DNS 包头最少需要 12 字节
    if (packet.size() < 12) {
        std::cerr << "[dns_parse]: packet too short, header must be at least 12 bytes\n";
        return domains;
    }

    // 读取 QDCOUNT（问题数）
    uint16_t qdcount = (packet[4] << 8) | packet[5];
    if (qdcount == 0) {
        std::cerr << "[dns_parse]: QDCOUNT is zero, no query domain to extract\n";
        return domains;
    }

    size_t pos = 12; // 问题部分从字节 12 开始
    for (int i = 0; i < qdcount; ++i) {
        // 读取第 i 个域名
        auto domain_opt = read_domain_name(packet, pos, packet.size());

        if (!domain_opt.has_value()) {
            std::cerr << "[dns_parse]: invalid domain name at query index " << i << "\n";
            break;
        }

        domains.push_back(*domain_opt);

        // 跳过 QTYPE (2 bytes) + QCLASS (2 bytes)
        if (pos + 4 > packet.size()) {
            std::cerr << "[dns_parse]: packet too short to read QTYPE/QCLASS at query index " << i << "\n";
            break;
        }
        pos += 4;
    }

    return domains;
}
