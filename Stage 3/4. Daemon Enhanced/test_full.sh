#!/bin/bash
# test_full.sh

echo "=== Test 5: Full Integration Test ==="

sudo truncate -s 0 /var/log/syslog

./doorbellod &
DAEMON_PID=$!
echo "Daemon started: $DAEMON_PID"
sleep 2

for i in {1..3}; do
    echo ""
    echo "=== Cycle $i: Start ==="
    echo "1" | sudo tee /dev/motion0
    sleep 3
    
    echo "=== Cycle $i: Stop ==="
    echo "0" | sudo tee /dev/motion0
    sleep 2
done

echo ""
echo "Stopping daemon..."
sudo kill -SIGTERM $DAEMON_PID
sleep 1

echo ""
echo "=== Analysis ==="
echo ""
echo "State transitions:"
tail -300 /var/log/syslog | grep "State transition"

echo ""
echo "Thread lifecycle:"
tail -300 /var/log/syslog | grep -E "(thread started|thread stopped)"

echo ""
echo "Capture summary:"
tail -300 /var/log/syslog | grep "thread stopped.*captured"

echo ""
echo "✅ Test 5 Complete"
