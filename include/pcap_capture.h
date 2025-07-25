#ifndef PCAP_CAPTURE_H
#define PCAP_CAPTURE_H
#pragma once

#include <atomic>
#include <string>
#include "ThreadSafeQueue.h"

/**
 * @file pcap_capture.h
 * @brief 基于 libpcap 的 DNS 抓包接口定义头文件
 *
 * 提供启动抓包功能和跨线程通信的域名队列定义。
 */

// DNS 协议使用的默认端口
constexpr int DNS_PORT = 53;

// 以太网帧头长度（固定为 14 字节）
constexpr int ETHERNET_HEADER_LEN = 14;

// 控制抓包是否停止的原子标志位（用于主线程通知退出）
extern std::atomic<bool> stop_processing;

// 域名结果的线程安全队列（供处理线程消费）
extern ThreadSafeQueue<std::string> domain_queue;

/**
 * @brief 启动指定网卡的 DNS 报文抓取
 * 
 * 使用 libpcap 打开指定网卡并启动数据包抓取，解析出 DNS 请求域名并推入 domain_queue 队列。
 * 该函数会阻塞运行直到 stop_processing 被置为 true。
 *
 * @param dev 网卡名称（如 "eth0", "ens33"）
 */
void start_packet_capture(const std::string& dev);

/**
 * @brief 停止抓包线程
 * 
 * 将 stop_processing 设置为 true，用于通知抓包线程安全退出。
 * 通常由主线程或信号处理器调用。
 */
void stop_packet_capture();

#endif // PCAP_CAPTURE_H
