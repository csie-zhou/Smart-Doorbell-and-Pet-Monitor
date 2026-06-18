#!/bin/bash
# test_ring_buffer.sh

echo "=== Test 3: Ring Buffer Wrapping ==="

sudo truncate -s 0 /var/log/syslog

./doorbellod &
DAEMON_PID=$!
sleep 2

echo "Starting motion detection..."
echo "1" | sudo tee /dev/motion0

echo ""
echo "Streaming for 10 seconds to see multiple wraps..."
sleep 10

echo ""
echo "Stopping..."
echo "0" | sudo tee /dev/motion0
sleep 2

sudo kill -SIGTERM $DAEMON_PID

echo ""
echo "=== Ring Buffer Wrap Analysis ==="
echo ""
echo "Wrap events:"
tail -200 /var/log/syslog | grep "Ring buffer wrapped"

echo ""
echo "Frame counts:"
tail -200 /var/log/syslog | grep "Captured.*frames" | tail -10

echo ""
echo "Thread summary:"
tail -200 /var/log/syslog | grep "thread stopped"

echo ""
echo "✅ Test 3 Complete"
