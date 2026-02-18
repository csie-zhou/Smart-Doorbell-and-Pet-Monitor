# Smart-Doorbell-and-Pet-Monitor
This project builds a professional smart doorbell and pet monitoring system using Linux kernel drivers for I2C and SPI hardware communication. The system detects motion at your front door, captures video, enables two-way audio communication via mobile app, and can differentiate between human visitors and pet activity. Unlike consumer products that are black boxes, this demonstrates deep embedded Linux skills including V4L2 video driver development, ALSA audio subsystem integration, interrupt-driven sensor management, and real-time streaming protocols.
## Key Differentiators
- Kernel-level video capture using V4L2 framework
-	Custom I2C drivers for motion and distance sensors with interrupt handling
-	SPI driver for camera module with DMA-based frame capture
-	ALSA audio driver for bidirectional communication
-	Real-time H.264 encoding and WebRTC streaming
-	Optional edge ML for pet vs. person classification

## System Architecture
### Hardware Layer
|**Bus**  |    **Component**      |      **Purpose**       |
|---------|-----------------------|------------------------|
|I2C      |PIR Motion Sensor (AM312)|Detect person approaching door, trigger recording|
|I2C      |ToF Distance Sensor (VL53L1X)|Measure distance - differentiate person at door vs. passing by|
|I2C      |OLED Display (SSD1306) |Local status indicator (Recording, Connected, Battery)|
|SPI      |Camera Module (OV2640)  |Capture 720p/1080p video, person/pet detection|
|SPI      |Flash Chip (W25Q128)   |Store 10-30 second video clips before upload|
|I2S      |I2S Microphone (INMP441)  |Capture audio from visitor|
|I2S      |I2S Amplifier (MAX98357A) |Play audio from mobile app to speaker|

### Kerenl Driver Layer
Each hardware component requires a custom kernel driver that interfaces with the Linux subsystems:
#### 1. Motion Sensor I2C Driver
**Driver Name**: `motion_sensor.ko`
  - Registers as I2C client driver with device tree binding
  - Configures PIR sensor sensitivity via I2C register writes
  - Implements interrupt handler for GPIO pin connected to sensor output
  - Exposes /dev/motion0 character device for event notification
  - Uses poll() mechanism so userspace can select() for motion events
#### 2. Distance Sensor I2C Driver
**Driver Name**: `vl53l1x.ko`
  - I2C driver for VL53L1X time-of-flight laser ranging
  - Implements sysfs interface: /sys/bus/i2c/devices/1-0029/distance_mm
  - Continuous measurement mode with configurable timing budget
  - ioctl interface for calibration and advanced configuration 
#### 3. Camera SPI Driver (V4L2 Framework)
**Driver Name**: `ov2640_spi.ko`
  - Registers as V4L2 video device: /dev/video0
  - SPI communication for camera register configuration
  - DMA-based frame capture from parallel camera interface
  - Supports multiple resolutions: 640x480, 1280x720, 1920x1080
  - Implements v4l2_ioctl_ops for userspace control (brightness, contrast, etc.)
  - MMAP buffer management for zero-copy frame access
#### 4. SPI Flash Driver
**Driver Name**: `spiflash.ko`
  - Character device interface: /dev/spiflash0
  - Block-based read/write operations
  - Erase operations (sector/block/chip erase)
  - Wear leveling and bad block management
  - DMA support for large transfers (video clips)
#### 5. Audio I2S Driver (ALSA Framework)
**Driver Name**: `doorbell_audio.ko`
  - Registers as ALSA PCM device: hw:0,0 (playback and capture)
  - I2S bus driver for microphone (INMP441) and amplifier (MAX98357A)
  - DMA circular buffer for low-latency audio streaming
  - 16-bit PCM, 16kHz/48kHz sample rates
  - ALSA controls for volume, mute, gain
  
### Userspace Application Layer
#### Main Daemon (C/C++)
**Process Name**: doorbellod
  - Event Loop: epoll() on /dev/motion0 for motion detection
  - Video Capture: Opens /dev/video0, uses V4L2 API to capture frames
  - H.264 Encoding: FFmpeg or hardware encoder (RPi has H264 encoder)
  - Storage: Writes clips to /dev/spiflash0 with filesystem abstraction
  - Streaming: WebRTC or RTSP server for mobile app connection
  - Audio: ALSA library for bidirectional audio streaming
  - Push Notifications: FCM (Firebase Cloud Messaging) to mobile app
#### Mobile Application (React Native or Flutter)
  - Receives push notifications when motion detected
  - Opens WebRTC connection to view live stream
  - Two-way audio with push-to-talk or auto-transmit
  - Video clip history stored in cloud (optional)
  - Settings: motion sensitivity, notification preferences, audio volume

## Development Roadmap
### Stage 1: Foundation & Basic I2C Drivers
**Goal**: Set up development environment and create basic I2C sensor drivers
  1. Install Raspberry Pi OS and kernel headers
  2. Configure I2C and SPI interfaces in /boot/config.txt
  3. Build skeleton I2C driver for motion sensor (probe, remove, module init/exit)
  4. Create device tree overlay for PIR sensor and VL53L1X
  5. Implement sysfs interface to read motion state
  6. Test with i2cdetect and verify device registration

**Deliverable:** Working I2C drivers, devices appear in /sys/bus/i2c/devices/

### Stage 2: Video Capture & SPI Drivers
**Goal**: Enable camera capture via V4L2 and build SPI flash driver  

  7. Enable camera interface on RPi (raspi-config or dtoverlay)  
  8. Test camera with v4l2-ctl and capture test images  
  9. Build SPI driver for W25Q128 flash chip  
  10. Implement character device interface for flash (/dev/spiflash0)  
  11. Add DMA support for large block transfers  
  12. Test write/read/erase operations from userspace  
  
**Deliverable:** Camera captures frames, SPI flash driver functional  

### Stage 3: Audio Subsystem & Event-Driven Architecture
**Goal**: Implement I2S audio drivers and build event-driven userspace daemon  

  13. Configure I2S interface on RPi GPIO pins
  14. Build ALSA driver for INMP441 microphone (capture)
  15. Build ALSA driver for MAX98357A amplifier (playback)
  16. Test audio recording and playback with arecord/aplay
  17. Create C daemon that uses epoll() to monitor /dev/motion0
  18. On motion event, trigger video capture and save to flash
  
**Deliverable:** Audio recording/playback works, daemon captures video on motion

### Stage 4: Streaming, Mobile App & Integration
**Goal**: Add video streaming, mobile app, and polish the system

  19. Implement H.264 encoding using hardware encoder or FFmpeg
  20. Set up WebRTC server for real-time streaming to mobile app
  21. Build React Native mobile app with video player and push-to-talk
  22. Integrate Firebase Cloud Messaging for push notifications
  23. Add error handling, logging, and watchdog timer for daemon
  24. Create systemd service for automatic startup
  25. Document architecture, create demo video

**Deliverable:** End-to-end system: motion detection


## Technical Deep Dives
### I2C Driver Implementation
#### KeyCompnents:
```C
static const struct i2c_device_id motion_sensor_id[] = { { "motion-sensor", 0 }, { } };

static int motion_probe(struct i2c_client *client, const struct i2c_device_id *id) {
  // Allocate device structure
  // Request GPIO interrupt
  // Register character device
  // Initialize wait queue for poll()
}

static struct i2c_driver motion_driver = {
  .driver = {
    .name = "motion-sensor",
    .of_match_table = motion_of_match,
  },
  .probe = motion_probe,
  .remove = motion_remove,
  .id_table = motion_sensor_id,
};
```
#### Device Tree Binding:
```C
&i2c1 {
  motion_sensor: motion@50 {
    compatible = "custom,motion-sensor";
    reg = <0x50>;
    interrupt-parent = <&gpio>;
    interrupts = <17 IRQ_TYPE_EDGE_RISING>;
  };
};
```

### SPI Driver with DMA
For high-speed data transfer (video frames, flash I/O), DMA is essential to avoid CPU blocking:
```C
static int spiflash_dma_write(struct spi_device *spi, const u8 *buf, size_t len) {
  struct spi_transfer xfer = { .tx_buf = buf, .len = len, };
  struct spi_message msg; spi_message_init(&msg);
  spi_message_add_tail(&xfer, &msg); return spi_sync(spi, &msg);
  // DMA if supported
}
```

### V4L2 Video Capture Pipeline
The userspace daemon uses V4L2 API to capture video frames:  

  26. Open `/dev/video0`
  27. Set format (VIDIOC_S_FMT): 1280x720, YUYV or H264
  28. Request buffers (VIDIOC_REQBUFS): typically 4 buffers
  29. Memory-map buffers (VIDIOC_QUERYBUF, mmap)
  30. Queue buffers (VIDIOC_QBUF)
  31. Start streaming (VIDIOC_STREAMON)
  32. Dequeue filled buffers (VIDIOC_DQBUF), process frame
  33. Re-queue buffer (VIDIOC_QBUF) for next frame

### ALSA Audio Driver Structure
ALSA drivers register PCM devices for playback and capture:
```C
static struct snd_pcm_ops doorbell_pcm_ops = {
  .open = doorbell_pcm_open,
  .close = doorbell_pcm_close,
  .hw_params = doorbell_pcm_hw_params,
  .prepare = doorbell_pcm_prepare,
  .trigger = doorbell_pcm_trigger,
  .pointer = doorbell_pcm_pointer,
};

// In probe: snd_pcm_new(card, "Doorbell PCM", 0, 1, 1, &pcm);
snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &doorbell_pcm_ops);
snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &doorbell_pcm_ops);
```

## Advanced Features & Extensions
### Pet vs. Person Detection (Edge ML)
Add on-device machine learning to classify detected motion:
  - Use TensorFlow Lite with MobileNet SSD model
  - Run inference on captured frame after motion detection
  - Classify: person, cat, dog, or unknown
  - Different notification types based on classification
  - Optional: Train custom model on your pets
### Cloud Integration
  - Store video clips in AWS S3 or Google Cloud Storage
  - Use AWS Rekognition or Google Vision API for facial recognition
  - Create shareable clip URLs for family members
  - Dashboard with analytics (visitors per day, peak times)
### Night Vision
  - Add IR LED array (940nm) controlled via GPIO
  - Use Pi NoIR camera (no IR filter) for night capture
  - Light sensor to auto-enable IR LEDs in low light
### Battery Backup
  - Add UPS HAT with 18650 batteries
  - I2C fuel gauge chip (MAX17048) to monitor battery level
  - Low-power mode when on battery (reduce frame rate, disable display)
  - Alert user when battery drops below threshold

## Testing & Debugging Strategies
### I2C Bus Debugging
  - Use i2cdetect to verify device addresses: i2cdetect -y 1
  - Read registers with i2cget: i2cget -y 1 0x50 0x00
  - Write registers with i2cset: i2cset -y 1 0x50 0x01 0xFF
  - Check dmesg for driver registration and probe messages
### SPI Debugging
  - Verify SPI is enabled: ls /dev/spidev*
  - Use spidev_test utility for loopback testing
  - Logic analyzer to verify CLK, MOSI, MISO timing
  - Check SPI mode (CPOL, CPHA) matches device datasheet
### Kernel Driver Debugging
  - Use printk() or pr_info() for logging
  - View logs: dmesg | tail -50
  - Set log level: echo 8 > /proc/sys/kernel/printk
  - Check module status: lsmod | grep motion
  - Unload/reload module: rmmod motion_sensor && insmod motion_sensor.ko
