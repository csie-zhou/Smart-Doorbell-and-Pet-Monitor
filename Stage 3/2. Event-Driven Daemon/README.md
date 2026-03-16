# 2. Event-Driven Daemon
### Building doorbellod — epoll-Based Userspace Application

## Goals

The daemon has three main components:

1. **Event Loop:** `epoll()` waits for events (motion, video ready, audio data)
2. **State Machine:** Manages system states (`IDLE → RECORDING → SAVING`)
3. **Worker Threads:** Handle video capture and audio in background

---

## Step 1: Create Mock Device Files

Since we're in QEMU without real hardware, create mock devices for testing:
```
# Create mock device nodes
sudo mknod /dev/motion0   c 245 0
sudo mknod /dev/spiflash0 c 246 0
sudo chmod 666 /dev/motion0 /dev/spiflash0

# Create simulation script
cat > ~/simulate_motion.sh << 'EOF'
#!/bin/
# Simulate motion detection every 10 seconds
while true; do
    echo "1" > /dev/motion0
    echo "[SIM] Motion triggered at $(date)"
    sleep 10
done
EOF

chmod +x ~/simulate_motion.sh
```

---

## Step 2: Daemon Data Structures

Create project directory:
```
mkdir -p ~/doorbell-daemon
cd ~/doorbell-daemon
nano doorbellod.c
```

Start with the core data structures:
```c
/*
 * doorbellod.c - Event-driven doorbell daemon
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <time.h>

#define MAX_EVENTS        10
#define VIDEO_BUFFER_SIZE (1024 * 1024)  // 1MB
#define AUDIO_BUFFER_SIZE 4096

/* System states */
enum doorbell_state {
    STATE_IDLE,        // Waiting for motion
    STATE_RECORDING,   // Capturing video/audio
    STATE_SAVING,      // Writing to flash
    STATE_STREAMING,   // Live view active
    STATE_ERROR
};

/* Daemon context */
struct doorbell_daemon {
    /* Device file descriptors */
    int motion_fd;
    int video_fd;
    int flash_fd;
    int audio_capture_fd;
    int audio_playback_fd;

    /* epoll instance */
    int epoll_fd;

    /* State machine */
    enum doorbell_state state;
    enum doorbell_state prev_state;

    /* Recording context */
    time_t recording_start;
    size_t bytes_captured;
    int    clip_number;

	/* Flash write position */
	size_t flash_write_offset;

    /* Worker threads */
    pthread_t video_thread;
    pthread_t audio_thread;
    int       video_running;
    int       audio_running;

    /* Buffers */
    unsigned char *video_buffer;
    unsigned char *audio_buffer;

    /* Control */
    volatile int running;
};

/* Forward declarations */
static void transition_state(struct doorbell_daemon *daemon,
                              enum doorbell_state new_state);
static void *video_capture_thread(void *arg);
static void *audio_capture_thread(void *arg);
static void handle_save_complete(struct doorbell_daemon *daemon);

/* Global daemon instance */
static struct doorbell_daemon *g_daemon = NULL;
```

---

## Step 3: State Machine Implementation
This function manages every state change in the system:
```c
/* State transition handler */
static void transition_state(struct doorbell_daemon *daemon,
                              enum doorbell_state new_state)
{
    if (daemon->state == new_state)
        return;		// If no state changes, do nothing

    syslog(LOG_INFO, "State transition: %d -> %d",
           daemon->state, new_state);

    /* Exit old state */
    switch (daemon->state) {
    case STATE_RECORDING:
        /* Stop recording threads */
        daemon->video_running = 0;
        daemon->audio_running = 0;
        break;
    case STATE_SAVING:
        /* Finalize flash write */
        fsync(daemon->flash_fd);	// forces kernel to flush write buffers to device
        break;
    default:
        break;	// State IDLE/ERROR no need to clean up
    }

    /* Update state */
    daemon->prev_state = daemon->state;
    daemon->state      = new_state;

    /* Enter new state */
    switch (new_state) {
    case STATE_IDLE:
        syslog(LOG_INFO, "Ready - waiting for motion");
        break;
    case STATE_RECORDING:
        daemon->recording_start = time(NULL);
        daemon->bytes_captured  = 0;
        daemon->clip_number++;

        /* Start worker threads */
		/* Only start video thread if device exists */
        if (daemon->video_fd >= 0) {
            daemon->video_running = 1;
            pthread_create(&daemon->video_thread, NULL,
                           video_capture_thread, daemon);
        } else {
            syslog(LOG_WARNING, "No video device - skipping video capture");
            daemon->video_running = 0;
        }

        /* Only start audio thread if device exists */
        if (daemon->audio_capture_fd >= 0) {
            daemon->audio_running = 1;
            pthread_create(&daemon->audio_thread, NULL,
                           audio_capture_thread, daemon);
        } else {
            syslog(LOG_WARNING, "No audio device - skipping audio capture");
            daemon->audio_running = 0;
        }
        break;
    case STATE_SAVING:
        syslog(LOG_INFO, "Saving %zu bytes to flash",
               daemon->bytes_captured);
        break;
    case STATE_ERROR:
        syslog(LOG_ERR, "Entered error state!");
        break;
    default:
        break;
    }
}
```

---

## Step 4: Worker Threads

### Video Capture Thread
```c
/* Video capture worker thread */
static void *video_capture_thread(void *arg)
{
    struct doorbell_daemon *daemon = arg;
    unsigned char frame[1024];		// 1KB temp buffer
    ssize_t bytes;

    syslog(LOG_INFO, "Video thread started");

    while (daemon->video_running) {
        /* Read frame from /dev/video0 (simulated) */
        bytes = read(daemon->video_fd, frame, sizeof(frame));	// Reads up to 1024 bytes from video device
		// bytes: actual bytes read

        if (bytes > 0) {
            /* Copy to video buffer */
            if (daemon->bytes_captured + bytes < VIDEO_BUFFER_SIZE) {
                memcpy(daemon->video_buffer + daemon->bytes_captured,
                       frame, bytes);
                daemon->bytes_captured += bytes;
            }

            /* Check if enough data captured (10 seconds) */
            time_t now = time(NULL);
            if (now - daemon->recording_start >= 10) {
                daemon->video_running = 0;
                break;
            }
        }
        usleep(33000);  // ~30 FPS
    }

    syslog(LOG_INFO, "Video thread stopped - captured %zu bytes",
           daemon->bytes_captured);

    /* Transition to saving state */
    transition_state(daemon, STATE_SAVING);
    return NULL;
}
```

### Audio Capture Thread
```c
/* Audio capture worker thread */
static void *audio_capture_thread(void *arg)
{
    struct doorbell_daemon *daemon = arg;
    unsigned char audio_data[AUDIO_BUFFER_SIZE];
    ssize_t bytes;

    syslog(LOG_INFO, "Audio thread started");

    while (daemon->audio_running) {
        /* Read from audio capture device */
        bytes = read(daemon->audio_capture_fd,
                     audio_data, sizeof(audio_data));
        if (bytes > 0) {
            /* Process audio (encode, buffer, etc.) */
            /* For now, just log */
        }
        usleep(20000);  // 50 Hz
    }

    syslog(LOG_INFO, "Audio thread stopped");
    return NULL;
}
```

---

## Step 5: Event Handlers
```c
/* Handle motion detection event */
static void handle_motion_event(struct doorbell_daemon *daemon)
{
    char    buf[64];
    ssize_t n;

    /* Read motion state */
    n = read(daemon->motion_fd, buf, sizeof(buf));
    if (n <= 0)
        return;		// 0: no data, shouldn't happen, epoll said data ready.

    syslog(LOG_INFO, "=== MOTION DETECTED ===");

    /* Only trigger if in IDLE state */
    if (daemon->state == STATE_IDLE) {
        transition_state(daemon, STATE_RECORDING);
        /* TODO: Send push notification */
        syslog(LOG_INFO, "Push notification sent");
    }
}

/* Handle saving complete */
static void handle_save_complete(struct doorbell_daemon *daemon)
{
    char    	 filename[128];
    ssize_t 	 written;
    unsigned int erase_addr = 0;
    size_t 	 sectors_needed;
    size_t	 i;

    /* Generate filename */
    snprintf(filename, sizeof(filename), "clip_%d_%ld.raw",
             daemon->clip_number, daemon->recording_start);

    #define FLASH_IOCTL_MAGIC   'F'
    #define FLASH_ERASE_SECTOR  _IOW(FLASH_IOCTL_MAGIC, 1, unsigned int)
    #define SECTOR_SIZE         4096
    #define FLASH_SIZE          (1024 * 1024)  /* 1MB */

    /* Skip if no flash device */
    if (daemon->flash_fd < 0) {
        syslog(LOG_WARNING, "No flash device available");
        transition_state(daemon, STATE_IDLE);
        return;
    }

    /* Skip if no data captured */
    if (daemon->bytes_captured == 0) {
        syslog(LOG_INFO, "No data to save (no video device)");
        transition_state(daemon, STATE_IDLE);
        return;
    }

    /* Check if enough space remains */
    if (daemon->flash_write_offset + daemon->bytes_captured > FLASH_SIZE) {
        syslog(LOG_WARNING, "Flash full! Wrapping to beginning");
        daemon->flash_write_offset = 0;
    }

    /* Calculate sectors needed — ceiling division */
    sectors_needed = (daemon->bytes_captured + SECTOR_SIZE - 1)
                     / SECTOR_SIZE;

    syslog(LOG_INFO, "Erasing %zu sectors at offset 0x%zx",
           sectors_needed, daemon->flash_write_offset);

    /* Erase all required sectors at current offset */
    for (i = 0; i < sectors_needed; i++) {
        erase_addr = daemon->flash_write_offset + (i * SECTOR_SIZE);

        if (erase_addr >= FLASH_SIZE) {
            syslog(LOG_ERR, "Erase address 0x%x exceeds flash size",
                   erase_addr);
            transition_state(daemon, STATE_IDLE);
            return;
        }

        if (ioctl(daemon->flash_fd,
                  FLASH_ERASE_SECTOR,
                  &erase_addr) < 0) {
            syslog(LOG_ERR, "Erase failed at 0x%x: %s",
                   erase_addr, strerror(errno));
            transition_state(daemon, STATE_IDLE);
            return;
        }
    }

    syslog(LOG_INFO, "Erase complete");

    /* Seek to current write position */
    if (lseek(daemon->flash_fd,
              daemon->flash_write_offset,
              SEEK_SET) < 0) {
        syslog(LOG_ERR, "lseek failed: %s", strerror(errno));
        transition_state(daemon, STATE_IDLE);
        return;
    }

    /* Write clip data */
    written = write(daemon->flash_fd,
                    daemon->video_buffer,
                    daemon->bytes_captured);

    if (written > 0) {
        syslog(LOG_INFO, "Saved %zd bytes to flash: %s "
               "(offset 0x%zx)",
               written, filename,
               daemon->flash_write_offset);

        /* Advance offset — align to sector boundary */
        daemon->flash_write_offset += sectors_needed * SECTOR_SIZE;

        syslog(LOG_INFO, "Next write offset: 0x%zx "
               "(%zu KB used of %d KB total)",
               daemon->flash_write_offset,
               daemon->flash_write_offset / 1024,
               FLASH_SIZE / 1024);

    } else {
        syslog(LOG_ERR, "Failed to write to flash: %s",
               strerror(errno));
        /* Don't advance offset — write failed */
    }

    /* Always return to idle */
    transition_state(daemon, STATE_IDLE);
}
```
`handle_motion_event()`:
- `if (daemon->state == STATE_IDLE)`: Only trigger in IDLE state, cause recording takes time too. Without it, if motion triggers and the state is RECORDING, it starts recording **again**. Which means 2 video threads running simultaneously, can cause race condition.


`handle_save_complete()`:
- No encoding, save to raw bytes from `device_read()`. Real hardware system would use .mp4 or .h264.
- With `daemon->flash_write_offset`, it saves the last offset:
	```
 	Save clip 1:  [clip1][0xFF 0xFF 0xFF...]
	Save clip 2:  [clip1][clip2][0xFF 0xFF...]
	Save clip 3:  [clip1][clip2][clip3][0xFF...]
 	```
---

## Step 6: Main Event Loop with epoll

This is the heart of the daemon:
```c
/* Main event loop */
static int daemon_event_loop(struct doorbell_daemon *daemon)
{
    struct epoll_event events[MAX_EVENTS]; // Ready events
    struct epoll_event ev;				   // Single event used when register a fd. Tells epoll to watch THIS fd for THESE events
    int nfds, i;						   // nfds: how many events kernel found in this cycle

    /* Create epoll instance */
    daemon->epoll_fd = epoll_create1(0);
    if (daemon->epoll_fd < 0) {
        syslog(LOG_ERR, "epoll_create1 failed: %s", strerror(errno));
        return -1;
    }

    /* Add motion sensor to epoll */
    ev.events  = EPOLLIN;				// wakes when data is available to read
    ev.data.fd = daemon->motion_fd;		// store fd to know who triggered later
    if (epoll_ctl(daemon->epoll_fd, EPOLL_CTL_ADD,
                  daemon->motion_fd, &ev) < 0) {
        syslog(LOG_ERR, "epoll_ctl (motion) failed: %s",
               strerror(errno));
        return -1;
    }

    syslog(LOG_INFO, "Event loop started - monitoring %d FDs", 1);
    syslog(LOG_INFO, "CPU usage: ~0%% (sleeping in epoll_wait)");

    /* Main loop */
    while (daemon->running) {
        /* Wait for events (SLEEPS HERE - zero CPU!) */
        nfds = epoll_wait(daemon->epoll_fd, events,
                          MAX_EVENTS, 1000);  // 1 sec timeout
        if (nfds < 0) {
            if (errno == EINTR)
                continue;  // Interrupted by signal
            syslog(LOG_ERR, "epoll_wait failed: %s", strerror(errno));
            break;
        }

        /* Process ready file descriptors */
        for (i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (fd == daemon->motion_fd)
                handle_motion_event(daemon);
        }

        /* State machine timeout handling */
        if (daemon->state == STATE_SAVING)
            handle_save_complete(daemon);

		/* If recording but no video/audio, go straight to saving */
        if (daemon->state == STATE_RECORDING &&
            !daemon->video_running &&
            !daemon->audio_running) {
            syslog(LOG_INFO, "No capture devices - skipping to save");
            transition_state(daemon, STATE_SAVING);
        }
    }

    close(daemon->epoll_fd);
    return 0;
}
```
- `daemon->epoll_fd`: works like a watchlist manager. `epoll_ctl()` control to list, `epoll_wait()` to see what's ready.
- `if (fd == daemon->motion_fd)`: our event is motion triggered by now, so the fd only allows from `motion_fd`

---

## Step 7: Initialization and Main
```c
/* Signal handler for clean shutdown */
static void signal_handler(int signum)
{
    if (g_daemon) {
        syslog(LOG_INFO, "Received signal %d - shutting down", signum);
        g_daemon->running = 0;
    }
}

/* Initialize daemon */
static int daemon_init(struct doorbell_daemon *daemon)
{
    memset(daemon, 0, sizeof(*daemon));

    /* Open log */
    openlog("doorbellod", LOG_PID | LOG_CONS, LOG_DAEMON);

    /* Allocate buffers */
    daemon->video_buffer = malloc(VIDEO_BUFFER_SIZE);
    daemon->audio_buffer = malloc(AUDIO_BUFFER_SIZE);
    if (!daemon->video_buffer || !daemon->audio_buffer) {
        syslog(LOG_ERR, "Failed to allocate buffers");
        return -1;
    }

    /* Open device files */
    daemon->motion_fd = open("/dev/motion0", O_RDONLY | O_NONBLOCK);
    if (daemon->motion_fd < 0) {
        syslog(LOG_WARNING, "Cannot open /dev/motion0: %s",
               strerror(errno));
        syslog(LOG_INFO, "Creating mock device...");
        /* Continue anyway for simulation */
    }

    daemon->video_fd = open("/dev/video10",    O_RDWR | O_NONBLOCK);
    daemon->flash_fd = open("/dev/spiflash0", O_RDWR);

    /* Audio disabled in QEMU - see Stage 3-1 Step 7 */
    daemon->audio_capture_fd  = -1;
    daemon->audio_playback_fd = -1;
    /* TODO: On real Pi, open ALSA PCM devices:
     * snd_pcm_open(&capture_handle,  "hw:1,0", SND_PCM_STREAM_CAPTURE,  0);
     * snd_pcm_open(&playback_handle, "hw:1,0", SND_PCM_STREAM_PLAYBACK, 0);
     */

    /* Initialize state */
    daemon->state       = STATE_IDLE;
    daemon->running     = 1;
    daemon->clip_number = 0;

	/* Initialize flash offset */
    daemon->flash_write_offset = 0;

    syslog(LOG_INFO, "Daemon initialized");
    return 0;
}

/* Cleanup daemon */
static void daemon_cleanup(struct doorbell_daemon *daemon)
{
    /* Stop worker threads */
    daemon->video_running = 0;
    daemon->audio_running = 0;
    if (daemon->video_thread)
        pthread_join(daemon->video_thread, NULL);
    if (daemon->audio_thread)
        pthread_join(daemon->audio_thread, NULL);

    /* Close devices */
    if (daemon->motion_fd >= 0) close(daemon->motion_fd);
    if (daemon->video_fd  >= 0) close(daemon->video_fd);
    if (daemon->flash_fd  >= 0) close(daemon->flash_fd);

    /* Free buffers */
    free(daemon->video_buffer);
    free(daemon->audio_buffer);

    closelog();		// Closes the connection to syslog daemon
    syslog(LOG_INFO, "Daemon cleanup complete");	// Reopens automatically
}

/* Main entry point */
int main(int argc, char *argv[])
{
    struct doorbell_daemon daemon;
    int ret;

    printf("Doorbell daemon starting...\n");

    /* Initialize daemon */
    if (daemon_init(&daemon) < 0) {
        fprintf(stderr, "Failed to initialize daemon\n");
        return 1;
    }
    g_daemon = &daemon;

    /* Setup signal handlers */
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    printf("Daemon ready - press Ctrl+C to stop\n");

    /* Run event loop */
    ret = daemon_event_loop(&daemon);

    /* Cleanup */
    daemon_cleanup(&daemon);

    printf("Daemon stopped\n");
    return ret;
}
```
`signal_handler()`:
- Handle shutdown signals (`Ctrl+C`: signal 2, `kill`: signal 15, `segfault`: signal 11...).

---

## Step 8: Compile

### Makefile
```makefile
CC      = gcc
CFLAGS  = -Wall -O2 -pthread
LDFLAGS = -pthread
TARGET  = doorbellod
SOURCES = doorbellod.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $>

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: all clean
```

### Compile
```
make
```

---

## Step 9: Modify motion_char.c for polling
### Add `poll()` to motion_char Driver
Copy paste this to the previous ` ~/kernel-modules/motion_char/motion_char.c
`:
```c
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "motion"
#define CLASS_NAME  "motion_class"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danny");
MODULE_DESCRIPTION("Motion sensor character device");

static int            major_number;
static struct class  *motion_class  = NULL;
static struct device *motion_device = NULL;

static int motion_state         = 0;  /* current state */
static int motion_data_available = 0; /* new unread event */

static DECLARE_WAIT_QUEUE_HEAD(motion_wait_queue);

static int device_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Motion device opened\n");
    return 0;
}

static ssize_t device_read(struct file *file, char __user *buffer,
                            size_t len, loff_t *offset)
{
    char msg[32];
    int  msg_len;

    msg_len = snprintf(msg, sizeof(msg), "motion=%d\n", motion_state);

    if (*offset >= msg_len){
		*offset = 0;	// Reset to read more than 1 motion triggeer
        return 0;
	}

    if (copy_to_user(buffer, msg, msg_len))
        return -EFAULT;

    *offset += msg_len;

    /* Clear after reading — ready for next event */
    motion_data_available = 0;

    return msg_len;
}

static ssize_t device_write(struct file *file, const char __user *buffer,
                              size_t len, loff_t *offset)
{
    char user_msg[32];

    if (len >= sizeof(user_msg))
        len = sizeof(user_msg) - 1;

    if (copy_from_user(user_msg, buffer, len))
        return -EFAULT;

    user_msg[len] = '\0';

    if (user_msg[0] == '1')
        motion_state = 1;
    else if (user_msg[0] == '0')
        motion_state = 0;

    /* Mark new data available and wake epoll */
    motion_data_available = 1;
    wake_up(&motion_wait_queue);  /* ← wakes epoll/select/poll */

    printk(KERN_INFO "Motion state set to %d\n", motion_state);
    return len;
}

static int device_release(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "Motion device closed\n");
    return 0;
}

static unsigned int motion_poll(struct file *file, poll_table *wait)
{
    poll_wait(file, &motion_wait_queue, wait);

    /* Only return POLLIN when new unread data exists */
    if (motion_data_available)
        return POLLIN | POLLRDNORM;

    return 0;  /* nothing to read — epoll sleeps */
}

static struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = device_open,
    .read    = device_read,
    .write   = device_write,
    .release = device_release,
    .poll    = motion_poll,
};

static int __init motion_char_init(void)
{
    printk(KERN_INFO "Motion: Initializing device\n");

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "Motion: Failed to register\n");
        return major_number;
    }

    motion_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(motion_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(motion_class);
    }

    motion_device = device_create(motion_class, NULL,
                                  MKDEV(major_number, 0),
                                  NULL, DEVICE_NAME "0");
    if (IS_ERR(motion_device)) {
        class_destroy(motion_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        return PTR_ERR(motion_device);
    }

    printk(KERN_INFO "Motion: Device created successfully\n");
    return 0;
}

static void __exit motion_char_exit(void)
{
    device_destroy(motion_class, MKDEV(major_number, 0));
    class_destroy(motion_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "Motion: Device unregistered\n");
}

module_init(motion_char_init);
module_exit(motion_char_exit);
```

### Recompile
```
cd ~/kernel-modules/motion_char
make clean && make
sudo rmmod motion_char
sudo insmod ~/kernel-modules/motion_char/motion_char.ko
sudo insmod ~/kernel-modules/spiflash/spiflash.ko
sudo chmod 666 /dev/motion0 /dev/spiflash0
```

---

## Step 10: Test

### Test the Daemon
```
# Verify all devices exist
ls -l /dev/motion0 /dev/spiflash0

# Terminal 1: Run daemon in background and watch logs
./doorbellod &
tail -f /var/log/syslog | grep doorbellod

# Terminal 2: Simulate motion
~/simulate_motion.sh
```
Expected:
```
=== MOTION DETECTED ===     ← trigger 1
State transition: 0 -> 1
State transition: 1 -> 2
State transition: 2 -> 0
Ready - waiting for motion

=== MOTION DETECTED ===     ← trigger 2
...
=== MOTION DETECTED ===     ← trigger 3
...
```
