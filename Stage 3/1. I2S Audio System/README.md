# 1. I2S Audio System
### Configure I2S & Build ALSA Driver with Circular DMA

## Overview
1. Understanding ALSA driver **structure**
2. Testing with **ALSA dummy driver**
3. Simulating audio with **sample files**
4. Writing code that **works on real Pi later**

## Step 1: I2S Hardware Configuration

### Understanding I2S Signals

**I2S (Inter-IC Sound) uses 3 main signals:**

| Signal | GPIO   | Purpose                                          |
|--------|--------|--------------------------------------------------|
| BCLK   | GPIO18 | Bit Clock — clock for each audio bit             |
| LRCLK  | GPIO19 | Left/Right channel select (also called WS — Word Select) |
| DOUT   | GPIO21 | Audio data **to** speaker (MAX98357A)            |
| DIN    | GPIO20 | Audio data **from** microphone (INMP441)         |

> **Key Point:** BCLK and LRCLK are **shared** between microphone and speaker. Only data lines are separate for full-duplex audio!

---

## Step 2: Create ALSA Dummy Driver
### Create folder for audio test
```
mkdir ~/kernel-modules/audio_test
cd ~/kernel-modules/audio_test
```

### Load ALSA Dummy Driver
```bash
# Load dummy sound card
sudo modprobe snd-dummy

# Verify it loaded
lsmod | grep snd_dummy

# Check sound cards
aplay -l
# Should show: card 0: Dummy [Dummy]

arecord -l
# Should also show: card 0: Dummy [Dummy]
```

---

## Step 3: Test Audio with Sample Files

### Create Test Audio Files
```bash
# Install audio tools
sudo apt install -y sox alsa-utils

# Create a test tone (440Hz, 5 seconds)
sox -n test_tone.wav synth 5 sine 440

# Create silence for capture testing
sox -n silence.wav trim 0 10

# Verify files created
ls -lh *.wav
```

### Test Playback (Dummy Device)
```bash
# Play to dummy device (no actual sound, but works!)
aplay -D plughw:0,0 test_tone.wav

# You'll see:
# Playing WAVE 'test_tone.wav' : Signed 16 bit Little Endian, Rate 48000 Hz, Stereo
```

### Test Capture (Dummy Device)
```bash
# Record from dummy device (captures silence)
arecord -D plughw:0,0 -d 5 -f cd captured.wav

# You'll see:
# Recording WAVE 'captured.wav' : Signed 16 bit Little Endian, Rate 44100 Hz, Stereo
```

---

## Step 4: Understand ALSA Driver Structure

### 1. Hardware Parameters
```c
static struct snd_pcm_hardware doorbell_pcm_hw = {
    .info = (SNDRV_PCM_INFO_MMAP |
             SNDRV_PCM_INFO_INTERLEAVED),
    .formats          = SNDRV_PCM_FMTBIT_S16_LE, // 16-bit audio
    .rates            = SNDRV_PCM_RATE_48000,     // 48kHz
    .channels_min     = 2,                         // Stereo
    .channels_max     = 2,
    .buffer_bytes_max = 32768,                     // 32KB buffer
    .period_bytes_min = 4096,                      // 4KB periods
};
```

**What this means:**
- Your driver supports 48kHz, 16-bit, stereo audio
- Uses 32KB circular buffer
- Callbacks every 4KB

### 2. Circular DMA Buffer Concept
### How Circular DMA Works
```
32KB ring buffer:
┌─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┬─────────┐
│ Period 0│ Period 1│ Period 2│ Period 3│ Period 4│ Period 5│ Period 6│ Period 7│
│  4KB    │  4KB    │  4KB    │  4KB    │  4KB    │  4KB    │  4KB    │  4KB    │
└─────────┴─────────┴─────────┴─────────┴─────────┴─────────┴─────────┴─────────┘
     ↑                                                                      ↑
  DMA reads here                                               wraps back here
  → callback fires every 4KB
  → ALSA fills next period while DMA reads current
  → gapless audio!
```
```c
desc = dmaengine_prep_dma_cyclic(
    chan,
    buffer_addr,    // Physical address
    32768,          // Total buffer (32KB)
    4096,           // Period size (4KB)
    direction,
    DMA_PREP_INTERRUPT
);
```

> **In QEMU:** Can't test real DMA, but code structure is correct!

### 3. DMA Callback
```c
static void playback_dma_callback(void *data)
{
    // Called every 4KB transferred
    audio->playback_pos += PERIOD_SIZE;

    // Circular wrap
    if (audio->playback_pos >= BUFFER_SIZE)
        audio->playback_pos = 0;

    // Notify ALSA
    snd_pcm_period_elapsed(substream);
}
```

- **On real hardware:** This runs automatically!
- **In QEMU:** We understand the concept!

---

## Step 5: Simplified Test Driver (QEMU Version)
```bash
mkdir -p ~/kernel-modules/audio_test
cd ~/kernel-modules/audio_test
nano audio_test.c
```
```c
/*
 * audio_test.c - Simple ALSA driver for testing concepts
 *
 * This is a SIMPLIFIED version for QEMU testing.
 * Tests driver structure without real I2S hardware.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>

#define DRIVER_NAME "audio-test"

/* Hardware parameters - same as real driver! */
static struct snd_pcm_hardware test_pcm_hw = {
    .info = (SNDRV_PCM_INFO_MMAP |
             SNDRV_PCM_INFO_INTERLEAVED),
    .formats          = SNDRV_PCM_FMTBIT_S16_LE,
    .rates            = SNDRV_PCM_RATE_48000,
    .rate_min         = 48000,
    .rate_max         = 48000,
    .channels_min     = 2,
    .channels_max     = 2,
    .buffer_bytes_max = 32768,
    .period_bytes_min = 4096,
    .period_bytes_max = 4096,
    .periods_min      = 2,
    .periods_max      = 8,
};

/* PCM operations - minimal for testing */
static int test_pcm_open(struct snd_pcm_substream *substream)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    runtime->hw = test_pcm_hw;
    pr_info("Audio Test: Stream opened\n");
    return 0;
}

static int test_pcm_close(struct snd_pcm_substream *substream)
{
    pr_info("Audio Test: Stream closed\n");
    return 0;
}

static int test_pcm_hw_params(struct snd_pcm_substream *substream,
                               struct snd_pcm_hw_params *params)
{
    pr_info("Audio Test: HW params set\n");
    return 0;
}

static int test_pcm_hw_free(struct snd_pcm_substream *substream)
{
    return 0;
}

static int test_pcm_prepare(struct snd_pcm_substream *substream)
{
    pr_info("Audio Test: Stream prepared\n");
    return 0;
}

static int test_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
    switch (cmd) {
    case SNDRV_PCM_TRIGGER_START:
        pr_info("Audio Test: Playback started\n");
        break;
    case SNDRV_PCM_TRIGGER_STOP:
        pr_info("Audio Test: Playback stopped\n");
        break;
    }
    return 0;
}

static snd_pcm_uframes_t test_pcm_pointer(struct snd_pcm_substream *substream)
{
    return 0;
}

static struct snd_pcm_ops test_pcm_ops = {
    .open      = test_pcm_open,
    .close     = test_pcm_close,
    .ioctl     = snd_pcm_lib_ioctl,
    .hw_params = test_pcm_hw_params,
    .hw_free   = test_pcm_hw_free,
    .prepare   = test_pcm_prepare,
    .trigger   = test_pcm_trigger,
    .pointer   = test_pcm_pointer,
};

/* Driver probe */
static int audio_test_probe(struct platform_device *pdev)
{
    struct snd_card *card;
    struct snd_pcm  *pcm;
    int ret;

    pr_info("Audio Test: Probing driver\n");

    /* Create sound card */
    ret = snd_card_new(&pdev->dev, -1, "AudioTest", THIS_MODULE,
                       0, &card);
    if (ret < 0)
        return ret;

    strcpy(card->driver,    "AudioTest");
    strcpy(card->shortname, "Audio Test Driver");
    strcpy(card->longname,  "QEMU ALSA Test Driver");

    /* Create PCM device */
    ret = snd_pcm_new(card, "Test PCM", 0, 1, 1, &pcm);
    if (ret < 0) {
        snd_card_free(card);
        return ret;
    }

    strcpy(pcm->name, "Test PCM");

    /* Set operations */
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &test_pcm_ops);
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,  &test_pcm_ops);

    /* Preallocate buffers */
    snd_pcm_set_managed_buffer_all(pcm,
        SNDRV_DMA_TYPE_CONTINUOUS,
	NULL,
        32768, 32768);

    /* Register card */
    ret = snd_card_register(card);
    if (ret < 0) {
        snd_card_free(card);
        return ret;
    }

    platform_set_drvdata(pdev, card);

    pr_info("Audio Test: Driver loaded successfully!\n");
    pr_info("Audio Test: Check with 'aplay -l'\n");
    return 0;
}

static int audio_test_remove(struct platform_device *pdev)
{
    struct snd_card *card = platform_get_drvdata(pdev);
    snd_card_free(card);
    pr_info("Audio Test: Driver removed\n");
    return 0;
}

static struct platform_driver audio_test_driver = {
    .probe  = audio_test_probe,
    .remove = audio_test_remove,
    .driver = {
        .name = DRIVER_NAME,
    },
};

/* Platform device for testing */
static struct platform_device *audio_test_device;

static int __init audio_test_init(void)
{
    int ret;

    /* Register driver */
    ret = platform_driver_register(&audio_test_driver);
    if (ret)
        return ret;

    /* Create device */
    audio_test_device = platform_device_register_simple(DRIVER_NAME,
                                                        -1, NULL, 0);
    if (IS_ERR(audio_test_device)) {
        platform_driver_unregister(&audio_test_driver);
        return PTR_ERR(audio_test_device);
    }

    return 0;
}

static void __exit audio_test_exit(void)
{
    platform_device_unregister(audio_test_device);
    platform_driver_unregister(&audio_test_driver);
}

module_init(audio_test_init);
module_exit(audio_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danny");
MODULE_DESCRIPTION("Simple ALSA Test Driver for QEMU");
MODULE_VERSION("1.0");
```
##### audio_test_probe
- `audio_test_probe(struct platform_device *pdev)`: `probe()` is automatically called by the kernel, as a setup function
  - `*pdev`: virtual device address
- `snd_card`: top-level ALSA object, handles everything
```
snd_card
    ↓
    └── snd_pcm (audio stream)
            ↓
            ├── playback substream
            └── capture substream
```
- Three different name fields ALSA uses in different places:
```
driver    → internal identifier    → /proc/asound/cards
shortname → brief display name     → aplay -l shows this
longname  → full description       → cat /proc/asound/cards shows this
```
- `snd_pcm_new(card, "Test PCM", 0, 1, 1, &pcm)`:
  - 0: device index (device 0 within the card)
  - 1: number of playback substreams
  - 1: number of capture substreams
- `platform_set_drvdata(pdev, card)`: Stores the card pointer (`probe()`) inside the platform device for later retrieval (`remove()`). 
  
 
### Makefile
```makefile
obj-m += audio_test.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

### Compile and Load
```bash
# Compile
make

# Load module
sudo insmod audio_test.ko

# Check dmesg
dmesg | tail -10
# Should see:
#   Audio Test: Probing driver
#   Audio Test: Driver loaded successfully!

# Verify ALSA device
aplay -l
# Should show: card X: AudioTest [Audio Test Driver]
```

---

## Step 6: Test Your Driver
```bash
# Play audio through your driver
aplay -D plughw:CARD=AudioTest test_tone.wav

# (Ctrl + C to quit)
# Check dmesg to see your driver responding
dmesg | tail -20
# You'll see:
#   Audio Test: Stream opened
#   Audio Test: HW params set
#   Audio Test: Stream prepared
#   Audio Test: Playback started
#   Audio Test: Playback stopped
#   Audio Test: Stream closed
```
---

## What To Learn

1. **ALSA driver structure:**
- PCM hardware parameters
- PCM operations (open, close, trigger, etc.)
- Buffer management concepts

2. **Circular DMA concepts:**
- Ring buffer architecture
- Period callbacks
- Automatic wraparound

3. **ALSA userspace testing:**
- `aplay`/`arecord` commands
- Device enumeration
- PCM device access

4. **Code that works on real Pi:**
- When you move to real Pi with INMP441/MAX98357A
- Just load the FULL `doorbell_audio.c` driver
- Same concepts, now with real I2S hardware!
