#ifndef DNS_PARSE_H
#define DNS_PARSE_H
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

/**
 * @file dns_parse.h
 * @brief 提供用于从DNS报文中解析域名（QNAME）的函数接口。
 *
 * 本模块用于提取DNS查询报文中的域名，支持处理压缩指针格式（RFC 1035），
 * 适用于网络抓包、DNS嗅探器、IDS系统等需要解析DNS数据的应用。
 */

/**
 * @brief 从DNS报文中提取所有查询的域名（仅支持Query部分，不含响应）
 *
 * @param packet 整个DNS报文的字节数组（UDP负载部分）
 * @return std::vector<std::string> 所有查询的问题域名列表（最多QDCOUNT个）
 *
 * 要求 packet 长度必须至少12字节（DNS header），否则返回空。
 * 若 QDCOUNT 为 0 或解析失败，则返回空列表。
 */
std::vector<std::string> extract_dns_queries(const std::vector<uint8_t>& packet);

/**
 * @brief 从DNS报文当前位置开始读取一个域名（支持压缩格式）
 *
 * @param packet DNS报文的字节数组（UDP负载）
 * @param pos 当前读取位置，调用后将被更新为域名后的新位置
 * @param max_len 报文的最大长度（用于越界保护）
 * @param jump_limit 跳转次数限制（防止死循环压缩解析）
 * @return std::string 解析得到的完整域名；若格式错误可返回空字符串或异常（具体取决于实现）
 *
 * 域名以标签序列表示，如 [3]www[6]google[3]com[0]，或使用压缩指针（0xC0开头）。
 * 函数支持递归解析压缩指针引用的域名。
 */
std::optional<std::string> read_domain_name(const std::vector<uint8_t>& packet, size_t& pos, size_t max_len, int jump_limit = 5);

#endif // DNS_PARSE_H
