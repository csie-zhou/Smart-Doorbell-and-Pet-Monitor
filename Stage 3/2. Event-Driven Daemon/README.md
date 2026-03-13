# 3. Event-Driven Daemon
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
```c
/* State transition handler */
static void transition_state(struct doorbell_daemon *daemon,
                              enum doorbell_state new_state)
{
    if (daemon->state == new_state)
        return;

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
        fsync(daemon->flash_fd);
        break;
    default:
        break;
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
        daemon->video_running = 1;
        daemon->audio_running = 1;
        pthread_create(&daemon->video_thread, NULL,
                       video_capture_thread, daemon);
        pthread_create(&daemon->audio_thread, NULL,
                       audio_capture_thread, daemon);

        syslog(LOG_INFO, "Recording clip #%d", daemon->clip_number);
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
    unsigned char frame[1024];
    ssize_t bytes;

    syslog(LOG_INFO, "Video thread started");

    while (daemon->video_running) {
        /* Read frame from /dev/video0 (simulated) */
        bytes = read(daemon->video_fd, frame, sizeof(frame));
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
        return;

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
    char    filename[128];
    ssize_t written;

    /* Generate filename */
    snprintf(filename, sizeof(filename), "clip_%d_%ld.raw",
             daemon->clip_number, daemon->recording_start);

    /* Write to flash */
    written = write(daemon->flash_fd,
                    daemon->video_buffer,
                    daemon->bytes_captured);
    if (written > 0) {
        syslog(LOG_INFO, "Saved %zd bytes to flash: %s",
               written, filename);
    } else {
        syslog(LOG_ERR, "Failed to write to flash: %s",
               strerror(errno));
    }

    /* Return to idle */
    transition_state(daemon, STATE_IDLE);
}
```

---

## Step 6: Main Event Loop with epoll

This is the heart of the daemon:
```c
/* Main event loop */
static int daemon_event_loop(struct doorbell_daemon *daemon)
{
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event ev;
    int nfds, i;

    /* Create epoll instance */
    daemon->epoll_fd = epoll_create1(0);
    if (daemon->epoll_fd < 0) {
        syslog(LOG_ERR, "epoll_create1 failed: %s", strerror(errno));
        return -1;
    }

    /* Add motion sensor to epoll */
    ev.events  = EPOLLIN;
    ev.data.fd = daemon->motion_fd;
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
    }

    close(daemon->epoll_fd);
    return 0;
}
```

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

    daemon->video_fd = open("/dev/video0",    O_RDWR | O_NONBLOCK);
    daemon->flash_fd = open("/dev/spiflash0", O_RDWR);

    /* Audio disabled in QEMU - see Week 3 Day 1-2 Step 7 */
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

    closelog();
    syslog(LOG_INFO, "Daemon cleanup complete");
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

---

## Step 8: Compile and Test

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

### Test the Daemon
```
# Terminal 1: Run daemon
./doorbellod

# Terminal 2: Simulate motion
~/simulate_motion.sh

# Watch logs
tail -f /var/log/syslog | grep doorbellod
```
