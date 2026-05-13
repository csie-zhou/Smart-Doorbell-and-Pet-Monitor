/*
 * doorbellod.c - Event-driven doorbell daemon
 */
#include <sys/ioctl.h>
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
#include <alsa/asoundlib.h> // For ALSA

/* ========== Event Loop ========== */
#define MAX_EVENTS        10	// Handle epoll fd events

/* ========== Video Configuration ========== */
#define VIDEO_WIDTH  640
#define VIDEO_HEIGHT 480
#define VIDEO_FORMAT_BPP 2  // YUYV = 2 bytes per pixel
#define FRAME_SIZE (VIDEO_WIDTH * VIDEO_HEIGHT * VIDEO_FORMAT_BPP)  // 614400 bytes = 614 KB

/* Ring buffer for smooth streaming (not storage!) */
#define VIDEO_RING_FRAMES 5  // Only 5 frames（166ms @ 30fps）
#define VIDEO_BUFFER_SIZE (VIDEO_RING_FRAMES * FRAME_SIZE)  // ~3 MB

/* ========== Audio Configuration ========== */
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_CHANNELS 2
#define AUDIO_FORMAT_BYTES 2  // S16_LE = 2 bytes per sample

/* ALSA DMA buffer configuration */
#define AUDIO_BUFFER_FRAMES 8192   // 32KB / 4 bytes = 8192 frames
#define AUDIO_PERIOD_FRAMES 1024   // 4KB / 4 bytes = 1024 frames
#define AUDIO_PERIODS 8            // 8 periods

/* Ring buffer for audio streaming */
#define AUDIO_RING_PERIODS 10	// Preserve 10 periods (~213ms)
#define AUDIO_PERIOD_BYTES (AUDIO_PERIOD_FRAMES * AUDIO_CHANNELS * AUDIO_FORMAT_BYTES)  // 4096 bytes
#define AUDIO_BUFFER_SIZE (AUDIO_RING_PERIODS * AUDIO_PERIOD_BYTES)  // ~40 KB

/* ========== SPI Configuration ========== */
#define SPI_MAX_TRANSFER_SIZE (64 * 1024)  // 64 KB per transfer
#define SPI_DEVICE "/dev/spidev0.0"

/* System states */
enum doorbell_state {
    STATE_IDLE,        // Waiting for motion
    STATE_STREAMING,   // Live view active
    STATE_ERROR
};

/* Daemon context */
struct doorbell_daemon {
    /* Device file descriptors */
    int motion_fd;	// /dev/motion0 (PIR sensor)
    int video_fd;	// /dev/video10 (camera)
    int spi_fd;		// /dev/spidev0.0 (SPI to monitor)

    /* ALSA handles */
    snd_pcm_t *audio_capture_handle;	// ALSA PCM capture

    /* epoll instance */
    int epoll_fd;

    /* State machine */
    enum doorbell_state state;
    enum doorbell_state prev_state;

    /* Recording context */
    time_t streaming_start;	// Start time for streaming
    // size_t bytes_captured;
    // int    clip_number;

    /* Motion Dectection */
    int motion_active;		// 0: no sig, 1: yes
    int last_motion_state;

    /* Video Ring Buffer Tracking */
    int video_write_position;	// pos(0~4)
    int video_read_position;	// SPI read
    int video_frames_captured;
    int video_frames_sent;
    int video_wraps;

    /* Audio Ring Buffer Tracking */
    int audio_write_position;	// pos(0~9)
    int audio_read_position;
    int audio_periods_captured;
    int audio_periods_sent;

    /* Worker threads */
    pthread_t video_thread;
    pthread_t audio_thread;
    pthread_t spi_thread;

    int       video_running;
    int       audio_running;
    int       spi_running;

    /* Ring Buffers */
    unsigned char *video_buffer;	// VIDEO_BUFFER_SIZE (3 MB)
    unsigned char *audio_buffer;	// AUDIO_BUFFER_SIZE (40 KB)

    /* Synchronization */
    pthread_mutex_t video_mutex;
    pthread_mutex_t audio_mutex;
    pthread_cond_t  video_cond;
    pthread_cond_t  audio_cond;

    /* Control */
    volatile int running;
};

/* Forward declarations */
static void transition_state(struct doorbell_daemon *daemon,
                              enum doorbell_state new_state);
static void *video_capture_thread(void *arg);
static void *audio_capture_thread(void *arg);
static void *spi_sender_thread(void *arg);
// static void handle_save_complete(struct doorbell_daemon *daemon);

/* Global daemon instance */
static struct doorbell_daemon *g_daemon = NULL;

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
    case STATE_STREAMING:
        /* Stop recording threads */
        daemon->video_running = 0;
        daemon->audio_running = 0;
        daemon->spi_running = 0;

	/* Wait for threads to finish */
        if (daemon->video_thread)
            pthread_join(daemon->video_thread, NULL);
        if (daemon->audio_thread)
            pthread_join(daemon->audio_thread, NULL);
        if (daemon->spi_thread)
            pthread_join(daemon->spi_thread, NULL);

	syslog(LOG_INFO, "All streaming threads stopped");
	break;

    case STATE_ERROR:
        /* Log error exit */
        syslog(LOG_INFO, "Exiting error state");
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
	daemon->motion_active = 0;
        break;

    case STATE_STREAMING:
	syslog(LOG_INFO, "Starting streaming mode...");

	/* Record streaming start time */
        daemon->streaming_start = time(NULL);

        /* Reset ring buffer positions */
        daemon->video_write_position = 0;
        daemon->video_read_position = 0;
        daemon->video_frames_captured = 0;
        daemon->video_frames_sent = 0;
        daemon->video_wraps = 0;

        daemon->audio_write_position = 0;
        daemon->audio_read_position = 0;
        daemon->audio_periods_captured = 0;
        daemon->audio_periods_sent = 0;

        /* Start video thread */
        if (daemon->video_fd >= 0) {
            daemon->video_running = 1;
	    if (pthread_create(&daemon->video_thread, NULL,
                              video_capture_thread, daemon) == 0) {
                syslog(LOG_INFO, "Video thread started");
            } else {
                syslog(LOG_ERR, "Failed to start video thread");
                daemon->video_running = 0;
            }
        } else {
            syslog(LOG_WARNING, "No video device - skipping video capture");
            daemon->video_running = 0;
        }

	/* Start audio thread */
	if (daemon->audio_capture_handle != NULL) {
            daemon->audio_running = 1;
            if (pthread_create(&daemon->audio_thread, NULL,
                              audio_capture_thread, daemon) == 0) {
                syslog(LOG_INFO, "Audio thread started");
            } else {
                syslog(LOG_ERR, "Failed to start audio thread");
                daemon->audio_running = 0;
            }
        } else {
            syslog(LOG_WARNING, "No audio device - skipping audio capture");
            daemon->audio_running = 0;
        }

	/* Start SPI sendor thread */
	if (daemon->spi_fd >= 0) {
            daemon->spi_running = 1;
            if (pthread_create(&daemon->spi_thread, NULL,
                              spi_sender_thread, daemon) == 0) {
                syslog(LOG_INFO, "SPI sender thread started");
            } else {
                syslog(LOG_ERR, "Failed to start SPI sender thread");
                daemon->spi_running = 0;
            }
        } else {
            syslog(LOG_WARNING, "No SPI device - skipping SPI transmission");
            daemon->spi_running = 0;
        }

	/* Check if at least video or audio is running */
        if (!daemon->video_running && !daemon->audio_running) {
            syslog(LOG_ERR, "No capture devices available - cannot stream");
            transition_state(daemon, STATE_ERROR);
        }

        break;
    case STATE_ERROR:
        syslog(LOG_ERR, "Entered error state!");
        break;
    default:
	syslog(LOG_WARNING, "Unknown state: %d", new_state);
        break;
    }
}

/* Video capture worker thread */
static void *video_capture_thread(void *arg)
{
    struct doorbell_daemon *daemon = arg;
    unsigned char *frame_buffer;
    ssize_t bytes;
    int frame_count = 0;

    /* Allocate full frame buffer */
    frame_buffer = malloc(FRAME_SIZE);
    if (!frame_buffer) {
        syslog(LOG_ERR, "Failed to allocate frame buffer");
        return NULL;
    }

    syslog(LOG_INFO, "Video thread started (ring buffer: %d frames, %d MB total)",
           VIDEO_RING_FRAMES, 
           (VIDEO_RING_FRAMES * FRAME_SIZE) / (1024 * 1024));

    while (daemon->video_running) {
        bytes = read(daemon->video_fd, frame_buffer, FRAME_SIZE);
        
        if (bytes > 0) {
            /* Calculate ring position */
            int ring_pos = frame_count % VIDEO_RING_FRAMES;
            size_t offset = ring_pos * FRAME_SIZE;
            
            /* Write to ring buffer */
            pthread_mutex_lock(&daemon->video_mutex);
            memcpy(daemon->video_buffer + offset, frame_buffer, bytes);
            daemon->video_frames_captured = frame_count + 1;
            daemon->video_write_position = ring_pos;
            pthread_mutex_unlock(&daemon->video_mutex);
            
            /* Signal SPI thread */
            pthread_cond_signal(&daemon->video_cond);
            
            frame_count++;
            
            /* Log progress every 30 frames */
            if (frame_count % 30 == 0) {
                syslog(LOG_INFO, "Video: captured %d frames | Ring pos: %d/%d",
                       frame_count, ring_pos, VIDEO_RING_FRAMES);
            }
            
            /* Detect ring buffer wrap */
            if (frame_count > 0 && frame_count % VIDEO_RING_FRAMES == 0) {
                daemon->video_wraps++;
                syslog(LOG_INFO, "Video ring buffer wrapped! (cycle %d, frame %d → 0)",
                       daemon->video_wraps, frame_count);
            }
        }
        
        usleep(1000);  // 1ms
    }

    syslog(LOG_INFO, "Video thread stopped - captured %d frames, %d wraps",
           frame_count, daemon->video_wraps);

    free(frame_buffer);
    return NULL;
}

static void *audio_capture_thread(void *arg)
{
    struct doorbell_daemon *daemon = arg;
    int err;
    int period_count = 0;
    int is_silent_warned = 0;
    
    /* Allocate buffer for one period */
    unsigned char *period_buffer = malloc(AUDIO_PERIOD_BYTES);
    if (!period_buffer) {
        syslog(LOG_ERR, "Failed to allocate audio buffer");
        return NULL;
    }
    
    syslog(LOG_INFO, "Audio thread started (ring buffer: %d periods, %d KB total)",
           AUDIO_RING_PERIODS, (AUDIO_RING_PERIODS * AUDIO_PERIOD_BYTES) / 1024);
    
    /* Prepare ALSA device */
    snd_pcm_prepare(daemon->audio_capture_handle);
    snd_pcm_start(daemon->audio_capture_handle);
    
    while (daemon->audio_running) {
	time_t start = time(NULL); // (Testing)

        /* ========== Read one period from ALSA DMA ========== */
        err = snd_pcm_readi(daemon->audio_capture_handle, 
                           period_buffer, 
                           AUDIO_PERIOD_FRAMES);

        time_t end = time(NULL);
	if (end - start > 1) {
	    syslog(LOG_WARNING, "Audio read took %ld seconds (slow!)", end - start);
        }

        /* ========== Error Handling ========== */
        if (err == -EPIPE) {
            syslog(LOG_WARNING, "Audio buffer overrun - recovering");
            snd_pcm_prepare(daemon->audio_capture_handle);
            snd_pcm_start(daemon->audio_capture_handle);  // ← 重新啟動！
            continue;
        } else if (err < 0) {
            syslog(LOG_ERR, "Audio read error: %s (code=%d)", snd_strerror(err), err);
            // ❌ 不要 break！嘗試恢復
            snd_pcm_prepare(daemon->audio_capture_handle);
            snd_pcm_start(daemon->audio_capture_handle);
            usleep(10000);  // 等待 10ms
            continue;
        }
        
        if (err != (int)AUDIO_PERIOD_FRAMES) {
            syslog(LOG_WARNING, "Audio short read: %d frames (expected %d)",
                   err, AUDIO_PERIOD_FRAMES);
        }
        
        /* ========== Silence Detection (只在前幾個 periods 檢查) ========== */
        if (!is_silent_warned && period_count < 5) {
            int is_silent = 1;
            short *samples = (short *)period_buffer;
            int num_samples = AUDIO_PERIOD_FRAMES * AUDIO_CHANNELS;
            
            for (int i = 0; i < num_samples; i++) {
                // 檢查是否有非零樣本（閾值 > 100 避免雜訊）
                if (samples[i] > 100 || samples[i] < -100) {
                    is_silent = 0;
                    break;
                }
            }
            
            if (is_silent && period_count == 3) {
                syslog(LOG_INFO, "Audio: capturing silence (no microphone input detected)");
                is_silent_warned = 1;
            } else if (!is_silent && period_count <= 3) {
                syslog(LOG_INFO, "Audio: capturing real audio data ✓");
                is_silent_warned = 1;
            }
        }
        
        /* ========== Write to Ring Buffer ========== */
        pthread_mutex_lock(&daemon->audio_mutex);
        
        // Calculate ring buffer position
        int ring_pos = period_count % AUDIO_RING_PERIODS;
        size_t write_offset = ring_pos * AUDIO_PERIOD_BYTES;
        
        // Write to ring buffer
        memcpy(daemon->audio_buffer + write_offset, 
               period_buffer, 
               AUDIO_PERIOD_BYTES);
        
        // Update tracking variables
        daemon->audio_write_position = ring_pos;
        daemon->audio_periods_captured = period_count + 1;
        
        pthread_mutex_unlock(&daemon->audio_mutex);
        
        /* Signal SPI thread that new audio is available */
        pthread_cond_signal(&daemon->audio_cond);
        
        period_count++;
        
        /* ========== Log Progress ========== */
        if (period_count % 10 == 0) {
            syslog(LOG_INFO, "Audio: captured %d periods | Ring pos: %d/%d",
                   period_count, ring_pos, AUDIO_RING_PERIODS);
        }
        
        /* ========== Detect Ring Buffer Wrap ========== */
        if (period_count > 0 && period_count % AUDIO_RING_PERIODS == 0) {
            int wrap_count = period_count / AUDIO_RING_PERIODS;
            syslog(LOG_INFO, "Audio ring buffer wrapped! (cycle %d, period %d → 0)",
                   wrap_count, period_count);
        }
    }
    
    syslog(LOG_INFO, "Audio thread stopped - captured %d periods, %d wraps",
           period_count, period_count / AUDIO_RING_PERIODS);
    
    free(period_buffer);
    return NULL;
}

/* SPI sender thread - sends video/audio to monitor */
static void *spi_sender_thread(void *arg)
{
    struct doorbell_daemon *daemon = arg;
    int video_frames_sent = 0;
    int audio_periods_sent = 0;
    
    syslog(LOG_INFO, "SPI sender thread started (monitoring ring buffers)");
    
    while (daemon->spi_running) {
	int data_sent = 0;	// Track data sent in this iteration

        /* ========== Check Video ========== */
        pthread_mutex_lock(&daemon->video_mutex);
        if (daemon->video_write_position != daemon->video_read_position) {
            /* TODO: Send video frame via SPI */
            daemon->video_read_position = (daemon->video_read_position + 1) % VIDEO_RING_FRAMES;
            daemon->video_frames_sent++;
            video_frames_sent++;
            data_sent = 1;
            
            if (video_frames_sent % 30 == 0) {
                syslog(LOG_INFO, "SPI: sent %d video frames", video_frames_sent);
            }
        }
        pthread_mutex_unlock(&daemon->video_mutex);

        /* ========== Check Audio ========== */
        pthread_mutex_lock(&daemon->audio_mutex);
        if (daemon->audio_write_position != daemon->audio_read_position) {
            /* TODO: Send audio period via SPI */
            daemon->audio_read_position = (daemon->audio_read_position + 1) % AUDIO_RING_PERIODS;
            daemon->audio_periods_sent++;
            audio_periods_sent++;
            data_sent = 1;
            
            if (audio_periods_sent % 10 == 0) {
                syslog(LOG_INFO, "SPI: sent %d audio periods", audio_periods_sent);
            }
        }
        pthread_mutex_unlock(&daemon->audio_mutex);

        /* ========== Adaptive Sleep ========== */
        if (data_sent) {
            // 有資料，快速檢查下一個
            idle_count = 0;
            usleep(1000);  // 1ms - 快速模式
        } else {
            // 沒資料，逐漸增加 sleep 時間
            idle_count++;
            if (idle_count < 10) {
                usleep(2000);   // 2ms
            } else if (idle_count < 50) {
                usleep(5000);   // 5ms
            } else {
                usleep(10000);  // 10ms - 省電模式
            }
        }
    }
    
    syslog(LOG_INFO, "SPI sender thread stopped - sent %d video frames, %d audio periods",
           video_frames_sent, audio_periods_sent);
    
    return NULL;
}

/* Handle motion detection event */
static void handle_motion_event(struct doorbell_daemon *daemon)
{
    char    buf[64];
    ssize_t n;

    /* Read current motion state */
    n = read(daemon->motion_fd, buf, sizeof(buf));
    if (n <= 0)
        return;

    /* Parse motion state */
    int current_motion = (buf[0] == '1') ? 1 : 0;

    /* =======  Signal Edge Detection ======= */

    /* Rising edge: 0->1 */
    if (current_motion == 1 && daemon->last_motion_state == 0) {
	syslog(LOG_INFO, "=== MOTION DETECTED (Signal HIGH) ===");
	printf("Motion detected! Starting streaming...\n");

	if (daemon->state == STATE_IDLE) {
	    daemon->motion_active = 1;
	    // transition_state(daemon, STATE_STREAMING);
	    printf("State: IDLE → STREAMING\n");
	}
    }

    /* Falling edge: 1->0 */
    else if (current_motion == 0 && daemon->last_motion_state == 1) {
	syslog(LOG_INFO, "=== MOTION CLEARED (Signal LOW) ===");
	printf("Motion cleared! Stopping streaming...\n");

	if (daemon->state == STATE_STREAMING) {
	    daemon->motion_active = 0;
	    // transition_state(daemon, STATE_IDLE);
	    printf("State: STREAMING → IDLE\n");
	}
    }

    /* Update the last state */
    daemon->last_motion_state = current_motion;
}

/* Handle saving complete /
static void handle_save_complete(struct doorbell_daemon *daemon)
{
    char    	 filename[128];
    ssize_t 	 written;
    unsigned int erase_addr = 0;
    size_t 	 sectors_needed;
    size_t	 i;

    /* Generate filename /
    snprintf(filename, sizeof(filename), "clip_%d_%ld.raw",
             daemon->clip_number, daemon->streaming_start);

    #define FLASH_IOCTL_MAGIC   'F'
    #define FLASH_ERASE_SECTOR  _IOW(FLASH_IOCTL_MAGIC, 1, unsigned int)
    #define SECTOR_SIZE         4096
    // #define FLASH_SIZE          (1024 * 1024)  /* 1MB */

    /* Skip if no flash device /
    if (daemon->flash_fd < 0) {
        syslog(LOG_WARNING, "No flash device available");
        transition_state(daemon, STATE_IDLE);
        return;
    }

    /* Skip if no data captured /
    if (daemon->bytes_captured == 0) {
        syslog(LOG_INFO, "No data to save (no video device)");
        transition_state(daemon, STATE_IDLE);
        return;
    }

    /* Check if enough space remains /
    if (daemon->flash_write_offset + daemon->bytes_captured > FLASH_SIZE) {
        syslog(LOG_WARNING, "Flash full! Wrapping to beginning");
        daemon->flash_write_offset = 0;
    }

    /* Calculate sectors needed — ceiling division /
    sectors_needed = (daemon->bytes_captured + SECTOR_SIZE - 1)
                     / SECTOR_SIZE;

    syslog(LOG_INFO, "Erasing %zu sectors at offset 0x%zx",
           sectors_needed, daemon->flash_write_offset);

    /* Erase all required sectors at current offset /
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

    /* Seek to current write position /
    if (lseek(daemon->flash_fd,
              daemon->flash_write_offset,
              SEEK_SET) < 0) {
        syslog(LOG_ERR, "lseek failed: %s", strerror(errno));
        transition_state(daemon, STATE_IDLE);
        return;
    }

    /* Write clip data /
    written = write(daemon->flash_fd,
                    daemon->video_buffer,
                    daemon->bytes_captured);

    if (written > 0) {
        syslog(LOG_INFO, "Saved %zd bytes to flash: %s "
               "(offset 0x%zx)",
               written, filename,
               daemon->flash_write_offset);

        /* Advance offset — align to sector boundary /
        daemon->flash_write_offset += sectors_needed * SECTOR_SIZE;

        syslog(LOG_INFO, "Next write offset: 0x%zx "
               "(%zu KB used of %d KB total)",
               daemon->flash_write_offset,
               daemon->flash_write_offset / 1024,
               FLASH_SIZE / 1024);

    } else {
        syslog(LOG_ERR, "Failed to write to flash: %s",
               strerror(errno));
        /* Don't advance offset — write failed /
    }

    /* Always return to idle /
    transition_state(daemon, STATE_IDLE);
}*/

/* Main event loop */
static int daemon_event_loop(struct doorbell_daemon *daemon)
{
    struct epoll_event events[MAX_EVENTS];
    struct epoll_event ev;
    int nfds, i;

    // printf("DEBUG: entering event loop\n");
    // printf("DEBUG: motion_fd = %d\n", daemon->motion_fd);
    // printf("DEBUG: running = %d\n", daemon->running);

    /* Create epoll instance */
    daemon->epoll_fd = epoll_create1(0);
    if (daemon->epoll_fd < 0) {
        syslog(LOG_ERR, "epoll_create1 failed: %s", strerror(errno));
        return -1;
    }
    // printf("DEBUG: epoll_fd = %d\n", daemon->epoll_fd);

    /* Add motion sensor to epoll — only if fd is valid */
    if (daemon->motion_fd >= 0) {
        ev.events  = EPOLLIN;
        ev.data.fd = daemon->motion_fd;
        if (epoll_ctl(daemon->epoll_fd, EPOLL_CTL_ADD,
                      daemon->motion_fd, &ev) < 0) {
	    printf("DEBUG: epoll_ctl failed: %s (fd=%d)\n",
           	   strerror(errno), daemon->motion_fd);
            syslog(LOG_ERR, "epoll_ctl (motion) failed: %s",
                   strerror(errno));
            return -1;
        }
        syslog(LOG_INFO, "Event loop started - monitoring %d FDs", 1);
    } else {
        syslog(LOG_WARNING, "motion_fd invalid - running without motion sensor");
        syslog(LOG_INFO, "Event loop started - no FDs monitored");
    }

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

        /* State machine timeout handling /
        if (daemon->state == STATE_SAVING)
            handle_save_complete(daemon);

	/* If recording but no video/audio, go straight to saving /
        if (daemon->state == STATE_STREAMING &&
            !daemon->video_running &&
            !daemon->audio_running) {
            syslog(LOG_INFO, "No capture devices - skipping to save");
            transition_state(daemon, STATE_SAVING);
        }*/
    }

    close(daemon->epoll_fd);
    return 0;
}

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
    int err;
    snd_pcm_hw_params_t *hw_params;
    unsigned int rate = AUDIO_SAMPLE_RATE;  // 48000

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
        }
    daemon->video_fd = open("/dev/video10",    O_RDWR | O_NONBLOCK);
        if (daemon->video_fd < 0)
            syslog(LOG_WARNING, "Cannot open /dev/video10 - video disabled");

    /* Audio disabled in QEMU - see Stage 3-1 Step 7 */
    // daemon->audio_capture_fd  = -1;
    /* daemon->audio_capture_fd = open("/dev/snd/pcmC0D0c", O_RDONLY | O_NONBLOCK);
	if (daemon->audio_capture_fd < 0) {
    		syslog(LOG_WARNING, "Cannot open audio: %s", strerror(errno));
	} else {
    		syslog(LOG_INFO, "Audio capture device opened!");
	}
    daemon->audio_playback_fd = -1;*/

    /* ======= For ALSA ======= */
    /* Try to open ALSA capture device */
    err = snd_pcm_open(&daemon->audio_capture_handle,
                       "hw:0,0",  // Card 0, Device 0
                       SND_PCM_STREAM_CAPTURE,
                       0);  // Blocking mode

    if (err < 0) {
        syslog(LOG_WARNING, "Cannot open ALSA audio device: %s",
               snd_strerror(err));
        daemon->audio_capture_handle = NULL;
    } else {
        syslog(LOG_INFO, "ALSA capture device opened!");

        /* ========== Configure hardware parameters ========== */
        snd_pcm_hw_params_malloc(&hw_params);
        snd_pcm_hw_params_any(daemon->audio_capture_handle, hw_params);

        /* Set access type */
        snd_pcm_hw_params_set_access(daemon->audio_capture_handle, hw_params,
                                       SND_PCM_ACCESS_RW_INTERLEAVED);

        /* Set format (S16_LE = 16-bit signed little-endian) */
        snd_pcm_hw_params_set_format(daemon->audio_capture_handle, hw_params,
                                       SND_PCM_FORMAT_S16_LE);

        /* Set channels (2 = stereo) */
        snd_pcm_hw_params_set_channels(daemon->audio_capture_handle, hw_params,
                                         AUDIO_CHANNELS);

        /* Set sample rate */
        snd_pcm_hw_params_set_rate_near(daemon->audio_capture_handle, hw_params,
                                          &rate, 0);

        /* Set buffer size (for DMA ring buffer) */
        snd_pcm_uframes_t buffer_size = AUDIO_BUFFER_FRAMES;
        snd_pcm_hw_params_set_buffer_size_near(daemon->audio_capture_handle,
                                                 hw_params, &buffer_size);

        /* Set period size */
        snd_pcm_uframes_t period_size = AUDIO_PERIOD_FRAMES;
        snd_pcm_hw_params_set_period_size_near(daemon->audio_capture_handle,
                                                 hw_params, &period_size, 0);

        /* Apply hardware parameters */
        err = snd_pcm_hw_params(daemon->audio_capture_handle, hw_params);
        if (err < 0) {
            syslog(LOG_ERR, "Cannot set ALSA hw params: %s", snd_strerror(err));
            snd_pcm_close(daemon->audio_capture_handle);
            daemon->audio_capture_handle = NULL;
        } else {
            syslog(LOG_INFO, "ALSA configured: %u Hz, %d channels, "
                   "buffer=%lu frames, period=%lu frames",
                   rate, AUDIO_CHANNELS, buffer_size, period_size);
        }

        snd_pcm_hw_params_free(hw_params);
    }

    /* TODO: On real Pi, open ALSA PCM devices:
     * snd_pcm_open(&capture_handle,  "hw:1,0", SND_PCM_STREAM_CAPTURE,  0);
     * snd_pcm_open(&playback_handle, "hw:1,0", SND_PCM_STREAM_PLAYBACK, 0);
     */

    /* Initialize state */
    daemon->state       = STATE_IDLE;
    daemon->running     = 1;
    daemon->motion_active = 0;
    daemon->last_motion_state = 0;

    /* Initialize ring buffer positions */
    daemon->video_write_position = 0;
    daemon->video_read_position = 0;
    daemon->audio_write_position = 0;
    daemon->audio_read_position = 0;

    /* Initialize mutex and condition variables */
    pthread_mutex_init(&daemon->video_mutex, NULL);
    pthread_mutex_init(&daemon->audio_mutex, NULL);
    pthread_cond_init(&daemon->video_cond, NULL);
    pthread_cond_init(&daemon->audio_cond, NULL);

    syslog(LOG_INFO, "Daemon initialized");
    return 0;
}

/* Cleanup daemon */
static void daemon_cleanup(struct doorbell_daemon *daemon)
{
    /* Stop worker threads */
    daemon->video_running = 0;
    daemon->audio_running = 0;
    daemon->spi_running = 0;

    if (daemon->video_thread)
        pthread_join(daemon->video_thread, NULL);
    if (daemon->audio_thread)
        pthread_join(daemon->audio_thread, NULL);
    if (daemon->spi_thread)
        pthread_join(daemon->spi_thread, NULL);

    /* Close devices */
    if (daemon->motion_fd >= 0) close(daemon->motion_fd);
    if (daemon->video_fd  >= 0) close(daemon->video_fd);
    if(daemon->spi_fd 	  >= 0) close(daemon->spi_fd);

    /* Close ALSA device */
    if (daemon->audio_capture_handle) {
        snd_pcm_drain(daemon->audio_capture_handle);  // Drain the buffer
        snd_pcm_close(daemon->audio_capture_handle);  // Close
        daemon->audio_capture_handle = NULL;
        syslog(LOG_INFO, "ALSA capture device closed");
    }

    /* Destroy mutex and condition variables */
    pthread_mutex_destroy(&daemon->video_mutex);
    pthread_mutex_destroy(&daemon->audio_mutex);
    pthread_cond_destroy(&daemon->video_cond);
    pthread_cond_destroy(&daemon->audio_cond);

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

    /* ===== For Testing ===== */
    printf("Testing state transitions...\n");
    
    /* Test 1: IDLE → STREAMING */
    printf("\n[Test 1] IDLE → STREAMING\n");
    transition_state(&daemon, STATE_STREAMING);
    sleep(2);
    
    /* Test 2: STREAMING → IDLE */
    printf("\n[Test 2] STREAMING → IDLE\n");
    transition_state(&daemon, STATE_IDLE);
    sleep(1);
    
    /* Test 3: IDLE → STREAMING → IDLE */
    printf("\n[Test 3] IDLE → STREAMING → IDLE\n");
    transition_state(&daemon, STATE_STREAMING);
    sleep(3);
    transition_state(&daemon, STATE_IDLE);
    /* ======================= */
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
