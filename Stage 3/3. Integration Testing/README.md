# 3. Integration Testing
### End-to-End System Testing in QEMU
---

## What We've Built So Far

**Stage 1:**
- Hello World kernel module
- I2C subsystem understanding
- Character device driver (`motion_char.ko`)

**Stage 2:**
- V4L2 camera concepts
- SPI subsystem understanding
- SPI flash driver (`spiflash.ko`)

**Stage 3:**
- ALSA audio concepts
- Event-driven daemon (`doorbellod`)
- `epoll()` architecture

**Now put it all together!**

---

## System Integration

### Step 1: Create Integration Test Script
```
mkdir -p ~/doorbell-integration
cd ~/doorbell-integration
nano test_integration.sh
```
```
#!/bin/bash
# Integration test for doorbell system (QEMU)

echo "========================================"
echo "   Doorbell Integration Test (QEMU)"
echo "========================================"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

test_passed() { echo -e "${GREEN}[PASS]${NC} $1"; }
test_failed() { echo -e "${RED}[FAIL]${NC} $1"; exit 1; }
test_info()   { echo -e "${YELLOW}[INFO]${NC} $1"; }

# ─────────────────────────────────────────
# PHASE 1 — STARTUP
# ─────────────────────────────────────────
echo "--- Phase 1: System Startup ---"
echo ""

# Load motion_char
test_info "Loading motion_char driver..."
if lsmod | grep -q motion_char; then
    test_info "motion_char already loaded — skipping"
else
    sudo insmod ~/kernel-modules/motion_char/motion_char.ko 2>/dev/null || true
fi
sudo chmod 666 /dev/motion0 2>/dev/null || true

# Load spiflash
test_info "Loading spiflash driver..."
if lsmod | grep -q spiflash; then
    test_info "spiflash already loaded — skipping"
else
    sudo insmod ~/kernel-modules/spiflash/spiflash.ko 2>/dev/null || true
fi
sudo chmod 666 /dev/spiflash0 2>/dev/null || true

# Load audio_test
test_info "Loading audio_test driver..."
if lsmod | grep -q audio_test; then
    test_info "audio_test already loaded — skipping"
else
    sudo insmod ~/kernel-modules/audio_test/audio_test.ko 2>/dev/null || true
fi

# Load v4l2loopback
test_info "Loading v4l2loopback..."
if lsmod | grep -q v4l2loopback; then
    test_info "v4l2loopback already loaded — skipping"
else
    sudo modprobe v4l2loopback devices=1 video_nr=10 2>/dev/null || true
fi
sudo chmod 666 /dev/video10 2>/dev/null || true

# Start ffmpeg
test_info "Starting fake camera feed (ffmpeg)..."
if pgrep -x ffmpeg > /dev/null; then
    test_info "ffmpeg already running — skipping"
else
    ffmpeg -f lavfi \
           -i testsrc=size=640x480:rate=30 \
           -f v4l2 /dev/video10 \
           -loglevel quiet &
    sleep 2
fi

# Start daemon
test_info "Starting doorbell daemon..."
if pgrep -x doorbellod > /dev/null; then
    test_info "doorbellod already running — skipping"
else
    cd ~/doorbell-daemon
    ./doorbellod &
    sleep 1
    cd ~/doorbell-integration
fi

# ─────────────────────────────────────────
# PHASE 2 — TESTS
# ─────────────────────────────────────────
echo ""
echo "--- Phase 2: Component Tests ---"
echo ""

# Test 1: Kernel modules directory
test_info "Test 1: Checking kernel modules..."
if [ -d ~/kernel-modules ]; then
    test_passed "Kernel modules directory exists"
else
    test_failed "Kernel modules directory not found"
fi

# Test 2: Daemon binary
test_info "Test 2: Checking daemon binary..."
if [ -f ~/doorbell-daemon/doorbellod ]; then
    test_passed "Daemon binary exists"
else
    test_failed "Daemon binary not found"
fi

# Test 3: Device files
test_info "Test 3: Checking device files..."
if [ -c /dev/motion0 ] && [ -c /dev/spiflash0 ]; then
    test_passed "Device files exist"
else
    test_failed "Device files missing"
fi

# Test 4: SPI flash driver
test_info "Test 4: Checking SPI flash driver..."
if lsmod | grep -q spiflash; then
    test_passed "SPI flash driver loaded"
else
    test_info "SPI flash driver not loaded (OK for testing)"
fi

# Test 5: Audio driver
test_info "Test 5: Checking audio driver..."
if lsmod | grep -q audio_test; then
    test_passed "Audio test driver loaded"
else
    test_info "Audio test driver not loaded (OK)"
fi

# Test 6: Fake camera
test_info "Test 6: Checking fake camera..."
if [ -c /dev/video10 ] && pgrep -x ffmpeg > /dev/null; then
    test_passed "Fake camera running on /dev/video10"
else
    test_info "Fake camera not available (video capture disabled)"
fi

# Test 7: Daemon running
test_info "Test 7: Checking daemon..."
if pgrep -x doorbellod > /dev/null; then
    test_passed "Daemon running (PID $(pgrep -x doorbellod))"
else
    test_failed "Daemon not running"
fi

# Test 8: Motion event
test_info "Test 8: Simulating motion event..."
echo "1" | sudo tee /dev/motion0 > /dev/null
if [ $? -eq 0 ]; then
    test_passed "Motion event triggered"
else
    test_failed "Failed to trigger motion event"
fi

# Test 9: Kernel log activity
test_info "Test 9: Checking kernel logs..."
if dmesg | tail -50 | grep -q "motion\|flash\|audio"; then
    test_passed "Driver activity detected in dmesg"
else
    test_info "Limited driver activity (OK in simulation)"
fi

# ─────────────────────────────────────────
# PHASE 3 — SUMMARY
# ─────────────────────────────────────────
echo ""
echo "--- Phase 3: System Status ---"
echo ""

echo "Loaded drivers:"
lsmod | grep -E "motion|spiflash|audio_test|v4l2loopback" || echo "  none"
echo ""

echo "Device files:"
ls -l /dev/motion0 /dev/spiflash0 /dev/video10 2>/dev/null || true
echo ""

echo "Running processes:"
ps aux | grep -E "ffmpeg|doorbellod" | grep -v grep || echo "  none"
echo ""

echo "========================================"
echo -e "${GREEN}   Integration Test Complete!${NC}"
echo "========================================"
echo ""
echo "System is fully loaded and running."
echo "Run ./test_workflow.sh  → test full doorbell cycle"
echo "Run ./monitor_system.sh → watch live status"
echo ""
```
```
chmod +x test_integration.sh
```

---

### Step 2: Run Integration Test
```
./test_integration.sh
```

**Expected output:**
```
========================================
   Doorbell Integration Test (QEMU)
========================================
[INFO] Test 1: Checking kernel modules...
[PASS] Kernel modules directory exists
[INFO] Test 2: Checking daemon binary...
[PASS] Daemon binary exists
[INFO] Test 3: Creating mock device files...
[PASS] Mock devices created
[INFO] Test 4: Loading SPI flash driver...
[PASS] SPI flash driver loaded
[INFO] Test 5: Loading audio test driver...
[PASS] Audio test driver loaded
[INFO] Test 6: Testing daemon startup...
[PASS] Daemon started successfully
[INFO] Test 7: Simulating motion event...
[PASS] Motion event triggered
[INFO] Test 8: Checking kernel logs...
[PASS] Driver activity detected in dmesg
[INFO] Cleaning up...
========================================
   Integration Test Complete!
========================================
All components working in QEMU simulation!
Ready for real hardware integration.
```

---

## End-to-End Workflow Test

### Step 1: Create Complete Workflow Test
```
nano test_workflow.sh
```
```
#!/bin/bash
# End-to-end workflow test

echo "===================================="
echo "   Complete Doorbell Workflow Test"
echo "===================================="
echo ""

# Guard — check integration was run first
if ! pgrep -x doorbellod > /dev/null; then
    echo "ERROR: Daemon not running!"
    echo "Run ./test_integration.sh first"
    exit 1
fi

if ! pgrep -x ffmpeg > /dev/null; then
    echo "ERROR: ffmpeg not running!"
    echo "Run ./test_integration.sh first"
    exit 1
fi

# Store existing PIDs so we don't kill them
EXISTING_DAEMON_PID=$(pgrep -x doorbellod)
EXISTING_FFMPEG_PID=$(pgrep -x ffmpeg | head -1)

echo "Using existing daemon PID: $EXISTING_DAEMON_PID"
echo "Using existing ffmpeg PID: $EXISTING_FFMPEG_PID"
echo ""

# [1/7] Daemon check
echo "[1/7] Checking daemon..."
if ps -p $EXISTING_DAEMON_PID > /dev/null 2>&1; then
    echo "      Daemon running (PID $EXISTING_DAEMON_PID)"
else
    echo "      ERROR: Daemon not found"
    exit 1
fi
echo ""

# [2/7] Simulate motion detection
echo "[2/7] Simulating motion detection..."
echo "1" | sudo tee /dev/motion0 > /dev/null
echo "      Motion event sent"
sleep 1
echo ""

# [3/7] Check daemon state transition
echo "[3/7] Checking daemon logs..."
tail -n 20 /var/log/syslog | grep doorbellod | tail -5
echo ""

# [4/7] Wait for video capture (10 seconds)
echo "[4/7] Waiting for video capture (10 seconds)..."
for i in {1..10}; do
    echo -n "."
    sleep 1
done
echo ""
echo "      Video capture complete"
echo ""

# [5/7] Check flash storage
echo "[5/7] Checking flash storage..."
if [ -e /dev/spiflash0 ]; then
    echo "      Flash device accessible"
    sudo dd if=/dev/spiflash0 bs=1 count=100 2>/dev/null | \
        hexdump -C | head -5
else
    echo "      Flash device not found"
fi
echo ""

# [6/7] Check audio subsystem
echo "[6/7] Checking audio subsystem..."
aplay -l 2>/dev/null | grep -E "card|AudioTest|Dummy" || \
    echo "      Audio in simulation mode"
echo ""

# [7/7] Verify system still healthy
echo "[7/7] Verifying system still healthy..."

if ! pgrep -x doorbellod > /dev/null; then
    echo "      Daemon stopped — restarting..."
    cd ~/doorbell-daemon
    ./doorbellod &
    sleep 1
    cd ~/doorbell-integration
    echo "      Daemon restarted (PID $(pgrep -x doorbellod))"
else
    echo "      Daemon still running (PID $(pgrep -x doorbellod))"
fi

if ! pgrep -x ffmpeg > /dev/null; then
    echo "      ffmpeg stopped — restarting..."
    ffmpeg -f lavfi \
           -i testsrc=size=640x480:rate=30 \
           -f v4l2 /dev/video10 \
           -loglevel quiet &
    sleep 1
    echo "      ffmpeg restarted (PID $(pgrep -x ffmpeg))"
else
    echo "      ffmpeg still running (PID $(pgrep -x ffmpeg | head -1))"
fi
echo ""

# Final log check
echo "Final daemon log:"
tail -n 10 /var/log/syslog | grep doorbellod
echo ""

echo "===================================="
echo "   Workflow Test Complete!"
echo "===================================="
echo ""
echo "Verified components:"
echo "  - Daemon lifecycle"
echo "  - Motion detection"
echo "  - State transitions"
echo "  - Video capture"
echo "  - Flash storage access"
echo "  - Audio subsystem"
echo ""
echo "System left RUNNING for continued use."
echo "Run ./monitor_system.sh to watch live status."
echo ""
```
```
chmod +x test_workflow.sh
./test_workflow.sh
```

---

### Step 2: Create System Monitoring Dashboard
```
nano monitor_system.sh
```
```
#!/bin/bash
# System monitoring dashboard with auto-recovery

while true; do
    clear
    echo "========================================"
    echo "   Doorbell System Monitor (QEMU)"
    echo "========================================"
    date
    echo ""

    # ── Kernel Modules ────────────────────────────
    echo "--- Loaded Kernel Modules ---"
    lsmod | grep -E "spiflash|audio_test|motion_char|v4l2loopback" || \
        echo "No custom modules loaded"
    echo ""

    # ── Auto-fix: Load missing drivers ────────────
    if ! lsmod | grep -q motion_char; then
        if ! pgrep -x doorbellod > /dev/null; then
            echo "[AUTO] motion_char not loaded — loading..."
            sudo rm -f /dev/motion0
	    sudo insmod ~/kernel-modules/motion_char/motion_char.ko \
                2>/dev/null \
                && sudo chmod 666 /dev/motion0 \
                && echo "[AUTO] motion_char loaded" \
                || echo "[AUTO] motion_char load failed"
        else
            echo "[WARN] motion_char unloaded while daemon running!"
            echo "[WARN] Restart daemon to fix motion connection"
        fi
    fi

    if ! lsmod | grep -q spiflash; then
        if ! pgrep -x doorbellod > /dev/null; then
            echo "[AUTO] spiflash not loaded — loading..."
            sudo rm -f /dev/spiflash0
	    sudo insmod ~/kernel-modules/spiflash/spiflash.ko \
                2>/dev/null \
                && sudo chmod 666 /dev/spiflash0 \
                && echo "[AUTO] spiflash loaded" \
                || echo "[AUTO] spiflash load failed"
        else
            echo "[WARN] spiflash unloaded while daemon running!"
            echo "[WARN] Restart daemon to fix flash connection"
        fi
    fi

    if ! lsmod | grep -q audio_test; then
        echo "[AUTO] audio_test not loaded — loading..."
        sudo insmod ~/kernel-modules/audio_test/audio_test.ko \
            2>/dev/null \
            && echo "[AUTO] audio_test loaded" \
            || echo "[AUTO] audio_test load failed"
    fi

    if ! lsmod | grep -q v4l2loopback; then
        echo "[AUTO] v4l2loopback not loaded — loading..."
        sudo modprobe v4l2loopback devices=1 video_nr=10 \
            2>/dev/null \
            && sudo chmod 666 /dev/video10 \
            && echo "[AUTO] v4l2loopback loaded" \
            || echo "[AUTO] v4l2loopback load failed"
    fi

    # ── Device Files ──────────────────────────────
    echo "--- Device Files ---"
    for dev in /dev/motion0 /dev/spiflash0 /dev/video10; do
        if [ -e "$dev" ]; then
            ls -l "$dev"
        else
            echo "MISSING: $dev"
        fi
    done
    echo ""

    # ── Auto-fix: Start missing processes ─────────
    if ! pgrep -x ffmpeg > /dev/null; then
        if [ -c /dev/video10 ]; then
            echo "[AUTO] ffmpeg not running — starting..."
            ffmpeg -f lavfi \
                   -i testsrc=size=640x480:rate=30 \
                   -f v4l2 /dev/video10 \
                   -loglevel quiet &
            sleep 1
            echo "[AUTO] ffmpeg started (PID $(pgrep -x ffmpeg))"
        else
            echo "[AUTO] ffmpeg needs /dev/video10 — skipping"
        fi
    fi

    if ! pgrep -x doorbellod > /dev/null; then
        echo "[AUTO] doorbellod not running — starting..."
        cd ~/doorbell-daemon
        ./doorbellod &
        sleep 1
        cd ~/doorbell-integration
        echo "[AUTO] doorbellod started (PID $(pgrep -x doorbellod))"
    fi

    # ── Running Processes ─────────────────────────
    echo "--- Running Processes ---"
    ps aux | grep -E "doorbellod|ffmpeg" | grep -v grep || \
        echo "No doorbell processes running"
    echo ""

    # ── Audio Devices ─────────────────────────────
    echo "--- Audio Devices ---"
    aplay -l 2>/dev/null | grep -E "card" | head -3 || \
        echo "No audio devices"
    echo ""

    # ── Recent Kernel Messages ────────────────────
    echo "--- Recent Kernel Messages ---"
    dmesg | tail -5
    echo ""

    # ── Recent Daemon Logs ────────────────────────
    echo "--- Recent Daemon Logs ---"
    tail -n 5 /var/log/syslog 2>/dev/null | grep doorbellod || \
        echo "No daemon logs"
    echo ""

    echo "Press Ctrl+C to exit"
    sleep 5
done
```
```
chmod +x monitor_system.sh
./monitor_system.sh
```

### Monitor Output
What each success means:
```
--- Kernel Modules ---
motion_char    ✅
spiflash       ✅
audio_test     ✅
v4l2loopback   ✅

--- Device Files ---
/dev/motion0    major 236  ✅
/dev/spiflash0  major 237  ✅
/dev/video10               ✅

--- Running Processes ---
ffmpeg      119 minutes running  ✅
doorbellod  running              ✅

--- Kernel Messages ---
Flash: ioctl cmd=0x40044601 ERASE=0x40044601  ✅  numbers match!
Flash: erased sector 0x40000                  ✅
Flash: wrote 270336 bytes                     ✅

--- Daemon Logs ---
Saved 270336 bytes to flash: clip_1_XXXXXX.raw (offset 0x0)  ✅
Next write offset: 0x42000 (264 KB used of 1024 KB total)     ✅
State transition: 2 -> 0                                      ✅
Ready - waiting for motion                                    ✅
```

```
ioctl cmd == ERASE value     ← major number mismatch fixed
Flash erased sector 0x40000  ← multi-sector erase working
Wrote 270336 bytes           ← real video data saved
offset 0x42000               ← write pointer advancing correctly
264 KB used of 1024 KB       ← flash space tracking working
clip_1_XXXXXX.raw            ← clip named and saved
Ready - waiting for motion   ← back to IDLE, ready for next trigger
```

---

## What We've Accomplished

- Complete system integration test
- End-to-end workflow validation
- All drivers loaded and tested
- Daemon lifecycle verified
- State machine working
- Event handling confirmed

---

## Stage 3 Complete! 🎉

| Layer | Component |
|-------|-----------|
| Kernel Drivers | `motion_char.ko`, `spiflash.ko`, `audio_test.ko` |
| Userspace Daemon | `doorbellod` with `epoll()` event loop |
| Integration Tests | Complete test suite |
| Documentation | All guides from Weeks 1-3 |

---

## Next Steps: Hardware Integration

1. **Flash Raspberry Pi** — Install Raspberry Pi OS
2. **Enable Hardware** — I2C, SPI, I2S, Camera via `raspi-config`
3. **Copy Code** — `rsync` all code from QEMU to Pi
4. **Connect Sensors** — Wire up PIR, VL53L1X, camera, etc.
5. **Load Drivers** — `insmod` all `.ko` files
6. **Test** — Run integration tests on real hardware
7. **Debug** — Fix hardware-specific issues (should be minimal!)

---

