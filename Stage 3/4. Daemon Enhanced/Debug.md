# Smart Doorbell Daemon - Debug Report

## Executive Summary
This document details the debugging process for synchronizing audio/video capture threads and optimizing SPI transmission throughput in a multi-threaded embedded streaming daemon. Through systematic log analysis, code inspection, and targeted fixes, we achieved:
 - ✅ Audio/Video synchronization: <30ms error (from 5+ seconds)
 - ✅ SPI throughput: 98%+ efficiency (from 77%)
 - ✅ Proper ring buffer operation with wrap detection

## Table of Contents
1. Problem 1: Audio/Video Time Desynchronization
2. Problem 2: Low SPI Transmission Throughput
3. Verification Methods
4. GDB Debugging Techniques
5. Final Results

## Problem 1: Audio/Video Time Desynchronization
### Initial Symptoms
<img width="430" height="39" alt="image" src="https://github.com/user-attachments/assets/7b7a1e58-776b-4abb-9d8a-889bc71ab97b" />

**Analysis:**
```bash
Video time: 336 frames ÷ 30 fps = 11.2 seconds
Audio time: 141 periods × 21.3ms = 3.0 seconds

Time difference: 11.2s - 3.0s = 8.2 seconds ❌
```
---
### Hypothesis & Investigation
#### Hypothesis 1: Audio thread exiting early due to errors
**Verification Steps:**
```bash
# Check for audio errors in logs
grep -i "audio.*error\|overrun" /var/log/syslog
```
---

#### Hypothesis 2: Audio device producing data slowly
**Verification Steps:**
```bash
# Test actual audio capture rate
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 10 test.wav
ls -lh test.wav
```

**Result:**
```bash
-rw-r--r-- 1 pi pi 1.9M May  7 08:50 test.wav

Expected: 10s × 48000Hz × 2ch × 2bytes = 1,920,000 bytes
Actual: 1.9 MB

Conclusion: Audio device is working correctly ✅
```

---

#### Hypothesis 3: Video capture rate exceeding expected 30 fps
**Code Inspection:**
// video_capture_thread()
while (daemon->video_running) {
    bytes = read(daemon->video_fd, frame_buffer, FRAME_SIZE);
    // ... process frame ...
    usleep(1000);  // Only 1ms delay
}

**Calculation:**
```bash
336 frames in 3 seconds (actual elapsed time)
= 112 fps ❌ (Expected: 30 fps)
```

**Root Cause Identified:** Video source (ffmpeg) was generating frames without rate limiting.

### Solution
Add `-re` flag to ffmpeg to enforce real-time rate
```bash
# Before: ffmpeg -f lavfi -i testsrc=size=640x480:rate=30 -f v4l2 /dev/video10
ffmpeg -re -f lavfi -i testsrc=size=640x480:rate=30 -f v4l2 /dev/video10
```

### Result
<img width="423" height="39" alt="image" src="https://github.com/user-attachments/assets/77cdca38-f5f7-4600-b1d5-6f9d634c6f7a" />

**Verification:**
```bash
Actual elapsed time: 2 seconds (08:53:26 → 08:53:28)

Video time: 61 frames ÷ 30 fps = 2.03 seconds ✅
Audio time: 94 periods × 21.3ms = 2.00 seconds ✅

Time difference: 2.03s - 2.00s = 0.03 seconds (30ms) ✅
```

---

## Problem 2: Low SPI Transmission Throughput
### Initial Symptoms
<img width="432" height="54" alt="image" src="https://github.com/user-attachments/assets/2c0ecdfa-e9d4-46d6-82ae-50142448887e" />

**Analysis:**
```bash
SPI efficiency: 20 ÷ 217 = 9% ❌
Missing: 193 frames (91% loss)
```

**Issue:** Issue: SPI thread unable to keep up with capture rate, causing data loss.

### Root Cause Analysis
**Code Inspection:**
```c
// spi_sender_thread() - Original
while (daemon->spi_running) {
    // Check video ring buffer
    // Check audio ring buffer
    
    usleep(10000);  // ← 10ms fixed delay
}
```

**Problem Identified:**
```bash
Video frame period: 33ms @ 30fps
SPI check interval: 10ms

Theoretical max: 10ms × 3 = 30ms per cycle
Actual: Often checking empty buffers, wasting time
```

**Inefficiency:** Fixed 10ms delay regardless of data availability.

### Solution: Adaptive Sleep
```c
static void *spi_sender_thread(void *arg)
{
    struct doorbell_daemon *daemon = arg;
    int video_frames_sent = 0;
    int audio_periods_sent = 0;
    int idle_count = 0;
    
    while (daemon->spi_running) {
        int data_sent = 0;
        
        /* Check and send video */
        pthread_mutex_lock(&daemon->video_mutex);
        if (daemon->video_write_position != daemon->video_read_position) {
            daemon->video_read_position = 
                (daemon->video_read_position + 1) % VIDEO_RING_FRAMES;
            video_frames_sent++;
            data_sent = 1;
        }
        pthread_mutex_unlock(&daemon->video_mutex);
        
        /* Check and send audio */
        pthread_mutex_lock(&daemon->audio_mutex);
        if (daemon->audio_write_position != daemon->audio_read_position) {
            daemon->audio_read_position = 
                (daemon->audio_read_position + 1) % AUDIO_RING_PERIODS;
            audio_periods_sent++;
            data_sent = 1;
        }
        pthread_mutex_unlock(&daemon->audio_mutex);
        
        /* Adaptive sleep based on activity */
        if (data_sent) {
            idle_count = 0;
            usleep(1000);   // 1ms - fast mode
        } else {
            idle_count++;
            if (idle_count < 10) {
                usleep(2000);   // 2ms
            } else if (idle_count < 50) {
                usleep(5000);   // 5ms
            } else {
                usleep(10000);  // 10ms - power saving
            }
        }
    }
    
    syslog(LOG_INFO, "SPI sender thread stopped - sent %d video frames, %d audio periods",
           video_frames_sent, audio_periods_sent);
    
    return NULL;
}
```

**Key Improvements:**
1. **Adaptive delay:** 1ms when active, gradually increases when idle
2. **Activity tracking:** `data_sent` flag and `idle_count` counter
3. **Power efficiency:** Reduces CPU usage when no data available

### Result
<img width="538" height="52" alt="image" src="https://github.com/user-attachments/assets/c38e0b5c-0044-41ca-9c03-b912c2b12766" />

**Verification:**
```bash
SPI efficiency: 60 ÷ 61 = 98.4% ✅

Improvement: 9% → 98.4%
```
---

### Verification Methods
#### 1. Log-Based Verification
**Key Log Patterns to Monitor:**
```bash
# Video capture rate
grep "Video thread stopped" /var/log/syslog

# Audio capture rate
grep "Audio thread stopped" /var/log/syslog

# SPI efficiency
grep "SPI sender thread stopped" /var/log/syslog

# Ring buffer wraps
grep "ring buffer wrapped" /var/log/syslog
```
#### 2. Mathematical Verification
**Video Timing:**
```bash
frames_captured ÷ fps = expected_duration

Example:
61 frames ÷ 30 fps = 2.03 seconds ✅
```

**Audio Timing:**
```bash
periods_captured × (period_size ÷ sample_rate) = expected_duration

Example:
94 periods × (1024 frames ÷ 48000 Hz) = 94 × 0.02133 = 2.00 seconds ✅
```

**Ring Buffer Wraps:**
```bash
expected_wraps = captured ÷ ring_size

Video: 61 ÷ 5 = 12.2 → 12 wraps ✅
Audio: 94 ÷ 10 = 9.4 → 9 wraps ✅
```

**SPI Efficiency:**
```bash
efficiency = sent ÷ captured × 100%

Video: 60 ÷ 61 = 98.4% ✅
Audio: 92 ÷ 94 = 97.9% ✅
```

#### 3. Audio Device Verification
**Test Command:**
```bash
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 10 test.wav
ls -lh test.wav

# Expected: -rw-r--r-- 1 pi pi 1.9M test.wav
# Calculation: 10 seconds × 48000 Hz × 2 channels × 2 bytes = 1,920,000 bytes = 1.9 MB
```

#### 4. ALSA DMA Status Check
**Verify DMA Buffer Configuration:**
```bash
cat /proc/asound/card0/pcm0c/sub0/hw_params
```
**Expected Output:**

<img width="639" height="139" alt="image" src="https://github.com/user-attachments/assets/08aa369d-6d0f-4cf6-a8ef-f8d5916a94c5" />

**Monitor DMA Hardware Pointer:**
```bash
watch -n 0.1 'cat /proc/asound/card0/pcm0c/sub0/status'
```
**Expected Output:**
```
hw_ptr: 0
hw_ptr: 1024
hw_ptr: 2048
...
hw_ptr: 8192
hw_ptr: 0  ← Wrapped! DMA ring buffer working ✅
```
?????????????
<img width="451" height="226" alt="image" src="https://github.com/user-attachments/assets/ac6d5902-deb2-4d50-a5cc-1e560e4d054d" />

If hw_ptr is static: DMA not running (driver issue).???????
---

## GDB Debugging Techniques
### Ring Buffer Wrap Detection
Verify that ring buffer position correctly wraps to 0.
```bash
# Create GDB command file
cat > gdb_ring_buffer.cmd << 'GDB_EOF'
# Set breakpoint in video thread
break video_capture_thread

# Create conditional breakpoint for first wrap
commands
  silent
  if frame_count == 5
    printf "=== First Wrap Detected ===\n"
    printf "frame_count: %d\n", frame_count
    printf "ring_pos: %d (should be 0)\n", ring_pos
    printf "video_wraps: %d (should be 1)\n", daemon->video_wraps
    printf "video_write_position: %d\n", daemon->video_write_position
    continue
  else
    continue
  end
end

run
quit
GDB_EOF

# Run GDB
sudo gdb -batch -x gdb_ring_buffer.cmd ./doorbellod
```
**Expected Output:**
```
=== First Wrap Detected ===
frame_count: 5
ring_pos: 0 ✅
video_wraps: 1 ✅
video_write_position: 0 ✅
```
**Failure Indicators:**
- `ring_pos != 0` → Modulo calculation error
- `video_wraps != 1` → Wrap counter not incrementing
- Program crashes → Buffer overflow

---

### 2. Memory Content Verification
Verify that frames are actually being written to the ring buffer.
```bash
cat > gdb_memory_check.cmd << 'GDB_EOF'
# Break after first frame captured
break video_capture_thread
commands
  silent
  if frame_count == 1
    printf "=== First Frame Captured ===\n"
    printf "Checking video buffer at offset 0...\n"
    x/64xb daemon->video_buffer
    
    # Check if buffer contains non-zero data
    set $i = 0
    set $nonzero = 0
    while $i < 64
      set $byte = *(unsigned char*)(daemon->video_buffer + $i)
      if $byte != 0
        set $nonzero = $nonzero + 1
      end
      set $i = $i + 1
    end
    
    printf "Non-zero bytes: %d/64\n", $nonzero
    if $nonzero > 0
      printf "✅ Buffer contains data\n"
    else
      printf "❌ Buffer is empty (all zeros)\n"
    end
    
    continue
  else
    continue
  end
end

run
quit
GDB_EOF

sudo gdb -batch -x gdb_memory_check.cmd ./doorbellod
```

**Expected Output:**
```
=== First Frame Captured ===
Checking video buffer at offset 0...
0x7f8000000: 0x80 0x80 0x00 0xff 0x80 0x80 0x00 0xff
0x7f8000008: 0x80 0x80 0x00 0xff 0x80 0x80 0x00 0xff
...
Non-zero bytes: 58/64
✅ Buffer contains data
```

**Failure Indicators:**
- All zeros → `memcpy()` not executing
- Garbage data → Buffer overflow or wrong offset

---

### 3. Thread Synchronization Check
Verify mutex is properly protecting ring buffer access.
```bash
cat > gdb_mutex_check.cmd << 'GDB_EOF'
# Set breakpoints on mutex lock/unlock
break pthread_mutex_lock
break pthread_mutex_unlock

commands 1
  silent
  printf "LOCK acquired by thread %d\n", pthread_self()
  where 2
  continue
end

commands 2
  silent
  printf "LOCK released by thread %d\n", pthread_self()
  continue
end

run
quit
GDB_EOF

sudo gdb -batch -x gdb_mutex_check.cmd ./doorbellod 2>&1 | head -50
```

**Expected Output Pattern:**
```
LOCK acquired by thread 140234567
#0  video_capture_thread
LOCK released by thread 140234567

LOCK acquired by thread 140234890  ← Different thread
#0  spi_sender_thread
LOCK released by thread 140234890
```

**Failure Indicators:**
- Same thread locks twice without unlock → Deadlock
- Lock never released → Mutex leak
- Crash during lock → Memory corruption

---

### 4. SPI Read/Write Position Tracking
Monitor read/write position divergence in real-time.
```bash
cat > gdb_spi_tracking.cmd << 'GDB_EOF'
# Break in SPI thread when sending data
break spi_sender_thread

# Create watchpoint on write position
watch daemon->video_write_position

commands
  silent
  printf "Position update: write=%d, read=%d, diff=%d\n", \
         daemon->video_write_position, \
         daemon->video_read_position, \
         (daemon->video_write_position - daemon->video_read_position + VIDEO_RING_FRAMES) % VIDEO_RING_FRAMES
  
  # Alert if buffer nearly full
  set $diff = (daemon->video_write_position - daemon->video_read_position + 5) % 5
  if $diff >= 4
    printf "⚠️  WARNING: Ring buffer nearly full! (%d/5)\n", $diff
  end
  
  continue
end

run
quit
GDB_EOF

sudo gdb -batch -x gdb_spi_tracking.cmd ./doorbellod
```

**Expected Output:**
```
Position update: write=0, read=0, diff=0
Position update: write=1, read=0, diff=1
Position update: write=2, read=1, diff=1
Position update: write=3, read=2, diff=1
...
```

**Failure Indicators:**
- `diff >= 4` sustained → SPI too slow, buffer overflow imminent
- `diff == 0` always → SPI reading faster than capture (impossible)
- Negative diff → Position tracking bug

---

### 5. Audio DMA Buffer State
Verify ALSA is using DMA and buffer is configured correctly.
```bash
cat > gdb_audio_dma.cmd << 'GDB_EOF'
# Break after ALSA initialization
break daemon_init
commands
  silent
  # Continue until after snd_pcm_hw_params() call
  finish
  finish
  
  # Check ALSA buffer configuration
  printf "=== ALSA Configuration ===\n"
  
  # These would require ALSA internal structures
  # Alternative: check via /proc at runtime
  shell cat /proc/asound/card0/pcm0c/sub0/hw_params
  
  continue
end

run
quit
GDB_EOF

sudo gdb -batch -x gdb_audio_dma.cmd ./doorbellod
```

**Alternative Runtime Check:**
```bash
# Start daemon in background
./doorbellod &
DAEMON_PID=$!

# Trigger streaming
echo "1" > /dev/motion0

# Monitor DMA status
watch -n 0.5 'cat /proc/asound/card0/pcm0c/sub0/status | grep hw_ptr'

# Expected: hw_ptr should increment continuously
# hw_ptr: 1024
# hw_ptr: 2048
# hw_ptr: 3072
```

---

## Final Results
### Complete System Verification
```bash
# In ~/doorbell-daemon
./test_full.sh
```
**Final Log Output:**
```
=== Cycle 1 (2 seconds) ===
May  7 08:53:28 raspberrypi doorbellod[19970]: Video thread stopped - captured 61 frames, 12 wraps
May  7 08:53:28 raspberrypi doorbellod[19970]: Audio thread stopped - captured 94 periods, 9 wraps
May  7 08:53:28 raspberrypi doorbellod[19970]: SPI sender thread stopped - sent 60 video, 92 audio

=== Cycle 2 (3 seconds) ===
May  7 08:53:32 raspberrypi doorbellod[19970]: Video thread stopped - captured 91 frames, 18 wraps
May  7 08:53:32 raspberrypi doorbellod[19970]: Audio thread stopped - captured 141 periods, 14 wraps
May  7 08:53:32 raspberrypi doorbellod[19970]: SPI sender thread stopped - sent 90 video, 139 audio
```

---

### Numerical Verification
| Metric | Cycle 1 (2s) | Cycle 2 (3s) | Status |
|--------|--------------|--------------|--------|
| **Video Time** | 61÷30 = 2.03s | 91÷30 = 3.03s | ✅ |
| **Audio Time** | 94×0.021 = 2.00s | 141×0.021 = 3.00s | ✅ |
| **Sync Error** | 0.03s (30ms) | 0.03s (30ms) | ✅ |
| **Video Wraps** | 61÷5 = 12 | 91÷5 = 18 | ✅ |
| **Audio Wraps** | 94÷10 = 9 | 141÷10 = 14 | ✅ |
| **SPI Video Eff** | 60/61 = 98.4% | 90/91 = 98.9% | ✅ |
| **SPI Audio Eff** | 92/94 = 97.9% | 139/141 = 98.6% | ✅ |

All metrics within acceptable tolerance. ✅
---

### Conclusion
Through systematic debugging using log analysis, mathematical verification, and targeted GDB techniques, we successfully:
1. ✅ Identified and fixed video source rate limiting issue (ffmpeg `-re` flag)
2. ✅ Optimized SPI thread with adaptive sleep mechanism
3. ✅ Verified ring buffer operation with wrap detection
4. ✅ Confirmed DMA operation through ALSA subsystem
5. ✅ Achieved <30ms audio/video synchronization
6. ✅ Achieved 98%+ SPI transmission efficiency

The final system demonstrates production-quality streaming performance with proper resource management and real-time guarantees.
---

## Appendix: Quick Reference Commands
```bash
# Verify audio device
arecord -D hw:0,0 -f S16_LE -r 48000 -c 2 -d 10 test.wav

# Check ALSA DMA configuration
cat /proc/asound/card0/pcm0c/sub0/hw_params

# Monitor DMA hardware pointer
watch -n 0.1 'cat /proc/asound/card0/pcm0c/sub0/status'

# Check system logs
tail -f /var/log/syslog | grep doorbellod

# Search for errors
grep -i "error\|overrun\|failed" /var/log/syslog

# Run full test suite
./test_full.sh

# Compile with debug symbols
gcc -g -o doorbellod doorbellod.c -lpthread -lasound

# Run under GDB
gdb ./doorbellod

# New virtual video (In terminal 2)
ffmpeg -re -f lavfi -i testsrc=size=640x480:rate=30 -f v4l2 /dev/video10
```


