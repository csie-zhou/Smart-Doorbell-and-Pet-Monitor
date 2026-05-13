#!/bin/bash
# test_gdb.sh

echo "=== Test 4: GDB Ring Buffer Inspection ==="

# 創建 GDB 腳本
cat > gdb_test.cmd << 'EOF'
# 設定 breakpoint 在第一次 wrap
break video_capture_thread
commands
  # 只在 frame_count == 5 時停下來（第一次 wrap）
  if frame_count == 5
    print frame_count
    print ring_pos
    print daemon->video_wraps
    print daemon->video_write_position
    continue
  else
    continue
  end
end

# 執行
run

# 程式結束後退出
quit
EOF

echo "Starting daemon in GDB..."
sudo gdb -batch -x gdb_test.cmd ./doorbellod &
GDB_PID=$!

sleep 3

echo "Triggering motion..."
echo "1" | sudo tee /dev/motion0

sleep 5

echo "Stopping motion..."
echo "0" | sudo tee /dev/motion0

wait $GDB_PID

rm gdb_test.cmd

echo ""
echo "✅ Test 4 Complete"
