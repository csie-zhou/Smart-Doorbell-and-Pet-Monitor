# Advanced
## Add the missing virtual devices
### video
### audio
```
# Install ALSA development library
sudo apt install -y libasound2-dev
```
Add to `doorbell.c`:
```c
#include <alsa/asoundlib.h>

/* In daemon struct — replace audio_capture_fd */
snd_pcm_t *audio_capture_handle;

/* In daemon_init() */
int err = snd_pcm_open(&daemon->audio_capture_handle,
                        "hw:0,0",
                        SND_PCM_STREAM_CAPTURE,
                        0);
if (err < 0) {
    syslog(LOG_WARNING, "Cannot open audio: %s",
           snd_strerror(err));
    daemon->audio_capture_handle = NULL;
}
```
Add to `Makefile`:
```
LDFLAGS = -pthread -lasound
```
## video_capture_thread
1. The previous we write to RAM, limited in fixed size (1KB ~= 34 seconds video).
2. The buffer location isn't resize after each motion detected. May cause => each detection bytes is cleared, but the buffer isn't.

Solution:  Write To File System, not RAM.
```c
static void *video_capture_thread(void *arg)
{
    struct doorbell_daemon *daemon = arg;
    unsigned char frame[4096];
    ssize_t bytes;
    char filepath[128];
    int out_fd;

    /* Create output file */
    snprintf(filepath, sizeof(filepath),
             "/tmp/clip_%d_%ld.raw",
             daemon->clip_number,
             daemon->recording_start);

    out_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        syslog(LOG_ERR, "Cannot create clip file");
        transition_state(daemon, STATE_SAVING);
        return NULL;
    }

    syslog(LOG_INFO, "Recording to %s", filepath);

    while (daemon->video_running) {
        bytes = read(daemon->video_fd, frame, sizeof(frame));
        if (bytes > 0) {
            /* Write to file — filesystem handles size */
            write(out_fd, frame, bytes);
            daemon->bytes_captured += bytes;

            time_t now = time(NULL);
            if (now - daemon->recording_start >= 60) {
                daemon->video_running = 0;
                break;
            }
        }
        usleep(33000);
    }

    close(out_fd);
    syslog(LOG_INFO, "Clip saved to %s (%zu bytes)",
           filepath, daemon->bytes_captured);

    transition_state(daemon, STATE_SAVING);
    return NULL;
}
```
```
Pros:  Unlimited size (disk space only limit)
       No RAM pressure
       Standard approach for real cameras
       Easy to retrieve/play back
Cons:  SD card speed may limit frame rate
       SD card wear (same as flash)

## What Real Doorbell Cameras Do

Ring, Nest, etc:
    Record to local SD card (Solution 3)
    Simultaneously stream to cloud
    Keep last 30-60 days of clips
    Old clips auto-deleted when storage full

## Recommendation For Your Project

QEMU simulation:    Solution 3 (write to /tmp/clip_X.raw)
                    Easy to verify: ls -lh /tmp/clip_*.raw

Real Pi hardware:   Solution 3 (write to /home/pi/clips/)
                    Or Solution 1 (write to real SPI flash)
                    for embedded systems with no filesystem
```
