# 1: V4L2 Camera Setup

## Goals

- Understand V4L2 architecture
- Install and configure `v4l2loopback` (virtual camera)
- List and query video devices
- Capture frames using `v4l2-ctl`
- Understand pixel formats and resolutions
- Write simple C program to capture video

---

## V4L2 Architecture

V4L2 is Linux's video capture/output subsystem. Here's how it's organized:

| Layer          | Description                                                        |
|----------------|--------------------------------------------------------------------|
| V4L2 Core      | Framework code in kernel (device registration, ioctls)            |
| Video Driver   | Camera-specific driver (e.g., `bcm2835-v4l2` for RPi camera)      |
| `/dev/video*`  | Device nodes exposed to userspace                                  |
| Userspace Apps | `v4l2-ctl`, `ffmpeg`, your C programs                              |

---

## Step 1: Install v4l2loopback

Since QEMU doesn't have a physical camera, we'll use `v4l2loopback` to create a virtual video device.

> **Note:** `v4l2loopback` may not be available in Raspberry Pi OS repos. If installation fails, we'll simulate video capture using test patterns instead. Both approaches teach V4L2 concepts equally well.

SSH into your QEMU VM:
```
ssh pi@localhost -p 5022
```

Install V4L2 utilities:
```
sudo apt update
sudo apt install -y v4l-utils
```

Try to install v4l2loopback (may not work, that's okay):
```
sudo apt install -y v4l2loopback-dkms v4l2loopback-utils
```

---

## Step 2: Check for Video Devices

List all video devices:
```
ls /dev/video*
```

**Possible outcomes:**
- No devices → QEMU doesn't expose video (expected)
- `/dev/video0` exists → May be virtual or emulated device

If `v4l2loopback` installed successfully, create a virtual device:
```
sudo modprobe v4l2loopback devices=1 video_nr=10 card_label="Virtual Camera"
```

Check again:
```
ls /dev/video*
```

Should see `/dev/video10`.

---

## Step 3: Query Video Device Capabilities

Use `v4l2-ctl` to inspect the device (replace `video10` with your device number):
```
v4l2-ctl --device=/dev/video10 --all
```

You'll see output like:
```
Driver Info:
    Driver name   : v4l2 loopback
    Card type     : Virtual Camera
    Bus info      : platform:v4l2loopback-010
    Driver version: 6.1.0
    Capabilities  : 0x85208002
        Video Capture
        Video Output
        Read/Write
        Streaming
```

### List Supported Formats
```
v4l2-ctl --device=/dev/video10 --list-formats
```

Common pixel formats:
- `YUYV` — YUV 4:2:2 (common for cameras)
- `RGB24` — 24-bit RGB
- `MJPEG` — Motion JPEG (compressed)

---

## Step 4: Understanding V4L2 Workflow

Video capture in V4L2 follows these steps:

1. **Open device:** `open("/dev/video0", O_RDWR)`
2. **Query capabilities:** `VIDIOC_QUERYCAP`
3. **Set format:** `VIDIOC_S_FMT` (resolution, pixel format)
4. **Request buffers:** `VIDIOC_REQBUFS` (typically 4 buffers)
5. **Map buffers:** `mmap()` each buffer to userspace
6. **Queue buffers:** `VIDIOC_QBUF` for each buffer
7. **Start streaming:** `VIDIOC_STREAMON`
8. **Capture loop:**
   - Dequeue filled buffer: `VIDIOC_DQBUF`
   - Process frame data
   - Re-queue buffer: `VIDIOC_QBUF`
9. **Stop streaming:** `VIDIOC_STREAMOFF`

---

## Step 5: Capture Test Frame with v4l2-ctl

Before writing code, let's capture using the command-line tool:
```
# Capture one frame, 640x480, YUYV format
v4l2-ctl --device=/dev/video10 \
          --set-fmt-video=width=640,height=480,pixelformat=YUYV \
          --stream-mmap \
          --stream-count=1 \
          --stream-to=frame.raw
```

> **Note:** This may fail if there's no actual video source feeding the loopback device. That's expected — the important part is understanding the commands.

---

## Step 6: Write Simple Capture Program

Create a directory for V4L2 code:
```
mkdir -p ~/v4l2-test
cd ~/v4l2-test
nano v4l2_capture.c
```

Here's a minimal V4L2 capture program (read-only, for learning):
```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

int main() {
    int fd;
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    // Open video device
    fd = open("/dev/video10", O_RDWR);
    if (fd < 0) {
        perror("Cannot open device");
        return 1;
    }
    printf("Device opened successfully\n");

    // Query capabilities
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return 1;
    }

    printf("Driver:  %s\n", cap.driver);
    printf("Card:    %s\n", cap.card);
    printf("Bus:     %s\n", cap.bus_info);
    printf("Version: %d.%d.%d\n",
           (cap.version >> 16) & 0xFF,
           (cap.version >>  8) & 0xFF,
            cap.version        & 0xFF);

    // Check capabilities
    if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)
        printf("Device supports Video Output\n");
    if (cap.capabilities & V4L2_CAP_STREAMING)
        printf("Device supports Streaming\n");

    // Get current format
    // Use OUTPUT type since our device doesn't support CAPTURE
    fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        close(fd);
        return 1;
    }

    printf("Current Format:\n");
    printf("  Width:        %d\n", fmt.fmt.pix.width);
    printf("  Height:       %d\n", fmt.fmt.pix.height);
    printf("  Pixel Format: %c%c%c%c\n",
            fmt.fmt.pix.pixelformat        & 0xFF,
           (fmt.fmt.pix.pixelformat >>  8) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
           (fmt.fmt.pix.pixelformat >> 24) & 0xFF);

    close(fd);
    printf("\nV4L2 test completed successfully!\n");
    return 0;
}
```

`ioctl(fd, command, data)` stands for **I/O Control**, which can let userspace to send command
to any kernel driver.  

Compile and run:
```
gcc -o v4l2_capture v4l2_capture.c
./v4l2_capture
```

Expected output:
```
Device opened successfully
Driver:  v4l2 loopback
Card:    Virtual Camera
Bus:     platform:v4l2loopback-010
Version: 6.1.21
Device supports Video Output
Device supports Streaming
Current Format:
  Width:        640
  Height:       480
  Pixel Format: YUYV

V4L2 test completed successfully!
```

---

## Step 7: Write Program with mmap

```
nano ~/v4l2-test/v4l2_mmap.c
```
```C
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define DEVICE      "/dev/video10"
#define NUM_BUFFERS 4
#define WIDTH       640
#define HEIGHT      480

/* Structure to hold each mapped buffer */
struct buffer {
    void   *start;   /* pointer to mapped memory */
    size_t  length;  /* size of buffer in bytes  */
};

int main() {
    int fd;
    struct v4l2_capability   cap;
    struct v4l2_format       fmt;
    struct v4l2_requestbuffers reqbuf;
    struct v4l2_buffer       buf;
    struct buffer            buffers[NUM_BUFFERS];
    int i;

    /* ── Step 1: Open device ───────────────────────────── */
    printf("=== Step 1: Open device ===\n");
    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    printf("Opened %s (fd=%d)\n\n", DEVICE, fd);

    /* ── Step 2: Query capabilities ────────────────────── */
    printf("=== Step 2: Query capabilities ===\n");
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return 1;
    }
    printf("Driver: %s  Card: %s\n", cap.driver, cap.card);

    /* Determine correct buffer type for this device */
    int buf_type;
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        printf("Mode: Video Capture\n\n");
    } else if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) {
        buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        printf("Mode: Video Output (no capture on this device)\n\n");
    } else {
        printf("ERROR: Neither capture nor output supported\n");
        close(fd);
        return 1;
    }

    /* ── Step 3: Set format ─────────────────────────────── */
    printf("=== Step 3: Set format ===\n");
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                = buf_type;
    fmt.fmt.pix.width       = WIDTH;
    fmt.fmt.pix.height      = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        close(fd);
        return 1;
    }
    printf("Format set: %dx%d YUYV\n", 
           fmt.fmt.pix.width, 
           fmt.fmt.pix.height);
    printf("Frame size: %d bytes\n\n", fmt.fmt.pix.sizeimage);

    /* ── Step 4: Request buffers ────────────────────────── */
    printf("=== Step 4: Request buffers ===\n");
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.count  = NUM_BUFFERS;     /* ask for 4 buffers        */
    reqbuf.type   = buf_type;
    reqbuf.memory = V4L2_MEMORY_MMAP; /* kernel allocates memory */

    if (ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("VIDIOC_REQBUFS");
        close(fd);
        return 1;
    }
    printf("Requested %d buffers, got %d buffers\n\n",
           NUM_BUFFERS, reqbuf.count);

    /* ── Step 5: mmap each buffer ───────────────────────── */
    printf("=== Step 5: mmap buffers ===\n");
    for (i = 0; i < reqbuf.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type   = buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        /* Ask kernel where this buffer is */
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close(fd);
            return 1;
        }

        /* Map it into our address space */
        buffers[i].length = buf.length;
        buffers[i].start  = mmap(
            NULL,               /* kernel chooses address    */
            buf.length,         /* size of buffer            */
            PROT_READ | PROT_WRITE, /* readable and writable */
            MAP_SHARED,         /* share with kernel         */
            fd,                 /* device file descriptor    */
            buf.m.offset        /* offset kernel gave us     */
        );

        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            close(fd);
            return 1;
        }

        printf("Buffer %d: mapped %zu bytes at %p\n",
               i, buffers[i].length, buffers[i].start);
    }
    printf("\n");

    /* ── Step 6: Queue all buffers ──────────────────────── */
    printf("=== Step 6: Queue buffers ===\n");
    for (i = 0; i < reqbuf.count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type   = buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            close(fd);
            return 1;
        }
        printf("Buffer %d queued\n", i);
    }
    printf("\n");

    /* ── Step 7: Start streaming ────────────────────────── */
    printf("=== Step 7: Start streaming ===\n");
    if (ioctl(fd, VIDIOC_STREAMON, &buf_type) < 0) {
        perror("VIDIOC_STREAMON");
        printf("REASON: Output-only device cannot stream as capture\n");
        /* still continue to show cleanup */
    } else {
        printf("Streaming started!\n\n");

        /* ── Step 8: Capture one frame ──────────────────── */
        printf("=== Step 8: Capture frame ===\n");
        memset(&buf, 0, sizeof(buf));
        buf.type   = buf_type;
        buf.memory = V4L2_MEMORY_MMAP;

        /* Dequeue a filled buffer */
        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            perror("VIDIOC_DQBUF");
        } else {
            printf("Got frame! Buffer %d, size %d bytes\n",
                   buf.index, buf.bytesused);
            printf("First 4 bytes: %02x %02x %02x %02x\n",
                   ((unsigned char*)buffers[buf.index].start)[0],
                   ((unsigned char*)buffers[buf.index].start)[1],
                   ((unsigned char*)buffers[buf.index].start)[2],
                   ((unsigned char*)buffers[buf.index].start)[3]);

            /* Requeue buffer for next frame */
            if (ioctl(fd, VIDIOC_QBUF, &buf) < 0)
                perror("VIDIOC_QBUF requeue");
        }

        /* ── Step 9: Stop streaming ─────────────────────── */
        printf("\n=== Step 9: Stop streaming ===\n");
        if (ioctl(fd, VIDIOC_STREAMOFF, &buf_type) < 0)
            perror("VIDIOC_STREAMOFF");
        else
            printf("Streaming stopped\n");
    }

    /* ── Step 10: Cleanup ───────────────────────────────── */
    printf("\n=== Step 10: Cleanup ===\n");
    for (i = 0; i < reqbuf.count; i++) {
        munmap(buffers[i].start, buffers[i].length);
        printf("Buffer %d unmapped\n", i);
    }

    close(fd);
    printf("Device closed\n");
    printf("\n=== MMAP streaming workflow complete ===\n");
    return 0;
}
```
```
gcc -o v4l2_mmap v4l2_mmap.c
./v4l2_mmap
```

```
Our Program                    Kernel
────────────                    ──────
VIDIOC_REQBUFS    →    "allocate 4 buffers in kernel memory"
                  ←    "ok, here are 4 buffer slots"

-- Step 5 (VIDIOC_QUERYBUF) ---
// kernel -> our program, "tell us about buffer i"
mmap()            →    "map buffer 0 into my address space"
                  ←    returns pointer to kernel buffer

mmap()            →    "map buffer 1 into my address space"
mmap()            →    "map buffer 2..."
mmap()            →    "map buffer 3..."

-- Step 6 (VIDIOC_QBUF) ---
// our program -> kernel, "buffer i is ours, fill it"
VIDIOC_QBUF       →    "buffer 0 is ready, fill it with a frame"
VIDIOC_QBUF       →    "buffer 1 is ready, fill it"
VIDIOC_QBUF       →    "buffer 2 is ready, fill it"
VIDIOC_QBUF       →    "buffer 3 is ready, fill it"

VIDIOC_STREAMON   →    "start capturing!"

── capture loop ──
VIDIOC_DQBUF      ←    "buffer 0 is filled with a frame"
  process frame        (read pixels directly via mmap pointer)
VIDIOC_QBUF       →    "buffer 0 is ready again, refill it"

VIDIOC_DQBUF      ←    "buffer 1 is filled..."
── end loop ──

VIDIOC_STREAMOFF  →    "stop capturing"
munmap()               unmap all buffers
close(fd)              close device
```

---

## What Learned

- **V4L2 architecture:** Core, drivers, device nodes
- **Video devices:** `/dev/video*` numbering
- **ioctls:** `VIDIOC_QUERYCAP`, `VIDIOC_G_FMT`, etc.
- **Pixel formats:** YUYV, RGB24, MJPEG
- **Buffer management:** MMAP streaming workflow
