# DNS 过滤器项目简介

本项目实现了一个基于生产者-消费者模型的 DNS 过滤系统，具备实时域名拦截、缓存管理与状态上报功能。

## 系统架构

### 1. 抓包与域名提取（生产者）

- 使用 `pcap` 在指定网卡上抓取 DNS 报文
- 解析出 DNS 查询中的域名
- 将域名写入一个线程安全队列

### 2. 缓存与上报处理（消费者）

- 由线程池管理的消费者从队列中取出域名
- 将域名写入 Redis 缓存
- 当新增域名达到阈值时，使用 `curl` 向 Nginx 服务器上报

### 3. 服务端处理

- 由 `nginx` 和 `dnsmasq` 组成
  - `nginx` 负责接收客户端上报的域名
  - 根据白名单判断是否得允许
  - 根据结果返回处理结果，并通过 HTTP 接口回写 Redis 修改域名状态和处理操作

### 4. 域名状态示例

```
+--------------------+---------+--------+
| Domain             | Status  | Action |
+--------------------+---------+--------+
| www.example.com    | FAKE    | DROP   |
| baidu.com          | FULL    | PERMIT |
| feishu.cn          | PEND    | DROP   |
+--------------------+---------+--------+
```

### 5. 定时上报

- 系统运行时包含一个定时线程，逐期收集各域名的访问统计数据，包括访问次数和当前状态
- 上报格式为 JSON，示例：

```
{
  "domain": "www.example.com",
  "status": "FAKE",
  "query_count": 10
}
```

## 配置与安装

本项目运行于 Linux 环境，依赖以下组件：

- Redis
- Nginx
- dnsmasq（使用默认配置）
- CMake

### 安装与配置步骤

1. 安装所需依赖：Redis、Nginx、dnsmasq、CMake
2. 修改以下配置文件中的内容以适配你的环境：
   - `RedisDNSCache.h` 中的 `REDIS_PASSWORD`（Redis 密码）
   - `dns_server.lua` 中的 `file` 路径（白名单文件路径）
   - `dns_parse.conf` 中：
     - `lua_package_path`（Lua 脚本搜索路径）
     - `content_by_lua_file`（主处理逻辑 Lua 文件路径）
   - `test.sh` 中 `dnsperf` 命令的 IP 地址（目标 DNS 服务器地址）

3. 运行代码

   ```
   进入build目录
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   make
   sudo ./dns_parse 你的网卡名称
   ```

4. 测试程序性能

   ```
   返回项目主路径
   sudo bash ./test.sh
   ```

   

## 应用场景

该系统适用于内网安全审计、DNS 劫持检测、恶意域名拦截等场景。



