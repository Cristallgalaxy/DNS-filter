# #!/bin/bash
# dnsperf -s 127.0.0.1 -d queries.txt -c 100 -l 10

# dnsperf -s 192.168.196.134 -d queries.txt -c 100 -l 10

#!/bin/bash
DURATION=3600
LOG_FILE="dns_test_$(date +%Y%m%d).log"

# 启动监控（使用 vmstat + iostat）
vmstat 5 > vmstat.log &
iostat -dx 5 > iostat.log &

# 运行测试
echo "=== TEST STARTED AT $(date) ===" >> $LOG_FILE
dnsperf -s 192.168.196.134 -d queries.txt -c 100 -l $DURATION >> $LOG_FILE 2>&1

# 清理监控进程
pkill -f "vmstat|iostat"
echo "=== TEST FINISHED AT $(date) ===" >> $LOG_FILE

# 打印关键结果
grep -E "Queries per second|Average Latency|Queries lost" $LOG_FILE