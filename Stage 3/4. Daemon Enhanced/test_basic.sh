#!/bin/bash
# test_basic.sh

echo "=== Test 1: Basic Daemon Startup ==="

# 清理舊的 log
sudo truncate -s 0 /var/log/syslog

# 啟動 daemon（背景執行）
./doorbellod &
DAEMON_PID=$!

echo "Daemon started with PID: $DAEMON_PID"
sleep 2

# 檢查是否還在執行
if ps -p $DAEMON_PID > /dev/null; then
    echo "✅ Daemon is running"
else
    echo "❌ Daemon crashed"
    exit 1
fi

# 檢查 log
echo ""
echo "=== Daemon Logs ==="
tail -20 /var/log/syslog | grep doorbellod

# 停止 daemon
echo ""
echo "Stopping daemon..."
sudo kill -SIGTERM $DAEMON_PID
sleep 1

if ps -p $DAEMON_PID > /dev/null; then
    echo "Force killing..."
    sudo kill -9 $DAEMON_PID
fi

echo "✅ Test 1 Complete"
