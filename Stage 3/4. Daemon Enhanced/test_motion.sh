#!/bin/bash
# test_motion.sh

echo "=== Test 2: Motion Detection & State Transitions ==="

# 清理 log
sudo truncate -s 0 /var/log/syslog

# 啟動 daemon
./doorbellod &
DAEMON_PID=$!
echo "Daemon PID: $DAEMON_PID"
sleep 2

echo ""
echo "=== Test 2.1: Motion Start (0→1) ==="
echo "1" | sudo tee /dev/motion0
sleep 2

echo ""
echo "Checking logs for STATE_STREAMING..."
tail -30 /var/log/syslog | grep "State transition.*1"
tail -30 /var/log/syslog | grep "Video thread started"
tail -30 /var/log/syslog | grep "Audio thread started"

echo ""
echo "=== Test 2.2: Let it stream for 5 seconds ==="
sleep 5

echo ""
echo "Checking capture progress..."
tail -50 /var/log/syslog | grep "Captured.*frames"

echo ""
echo "=== Test 2.3: Motion Stop (1→0) ==="
echo "0" | sudo tee /dev/motion0
sleep 2

echo ""
echo "Checking logs for STATE_IDLE..."
tail -30 /var/log/syslog | grep "State transition.*0"
tail -30 /var/log/syslog | grep "thread stopped"

echo ""
echo "=== Test 2.4: Motion Start Again ==="
echo "1" | sudo tee /dev/motion0
sleep 3

echo "Checking second streaming session..."
tail -30 /var/log/syslog | grep "Video thread started"

echo ""
echo "=== Test 2.5: Final Stop ==="
echo "0" | sudo tee /dev/motion0
sleep 2

# 停止 daemon
sudo kill -SIGTERM $DAEMON_PID
sleep 1

echo ""
echo "✅ Test 2 Complete"
echo ""
echo "=== Full Log Summary ==="
tail -100 /var/log/syslog | grep doorbellod | grep -E "(State transition|thread|MOTION|Captured)"
