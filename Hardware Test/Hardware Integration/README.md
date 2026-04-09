# Hardware Integration
## Part 1. GPIO Motion Driver for Hardware
### 1. Create Hardware-Ready Motion Driver
In QEMU verison we uses character devices for mock data. Now we need a real driver that:
- Monitors GPIO17 for interrupts
- Creates /dev/motion0 device
- Supports poll() for daemon
- Works with actual PIR sensor

```
# On Raspberry Pi
mkdir -p ~/doorbell/drivers/motion_gpio
cd ~/doorbell/drivers/motion_gpio
nano motion_gpio.c
```
```c
/*
 * motion_gpio.c - GPIO-based Motion Sensor Driver for HC-SR501
 *
 * Creates /dev/motion0 character device.
 * Supports poll() for efficient event notification.
 *
 * Compatible: Raspberry Pi 4, kernel 6.6+
 * Author: Danny
 *
 * ---- Why gpiod_lookup_table? ----
 * Kernel 6.6+ moved gpiochip_find() and gpiochip_get_desc() to internal
 * headers (gpio/gpiolib.h), making them inaccessible to out-of-tree modules.
 * The supported replacement for module_init-style drivers that cannot use
 * Device Tree is gpiod_add_lookup_table() + gpiod_get(), which is the
 * official "platform data" GPIO mapping path and is stable across 6.x.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio/consumer.h>   /* gpiod_get/put, gpiod_direction_input … */
#include <linux/gpio/machine.h>    /* gpiod_lookup_table, gpiod_add_lookup_table */
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>          /* atomic_t — IRQ-safe flag               */

#define DEVICE_NAME   "motion"
#define CLASS_NAME    "motion"
#define GPIO_PIN      17           /* BCM pin number on the 40-pin header     */

/* ------------------------------------------------------------------ */
/*  GPIO lookup table — platform-data mapping (no Device Tree needed)  */
/*                                                                      */
/*  gpiod_add_lookup_table() registers this table so that              */
/*  gpiod_get(dev, "motion", …) can resolve the descriptor by          */
/*  (chip_label, hw_offset) without touching any internal gpiolib API. */
/*                                                                      */
/*  "pinctrl-bcm2711" is the .label of the BCM2711 GPIO controller;    */
/*  visible in dmesg as "pinctrl-bcm2711".               */
/* ------------------------------------------------------------------ */
static struct gpiod_lookup_table motion_gpio_table = {
    .dev_id  = DEVICE_NAME "0",       /* must match the device name below   */
    .table   = {
        /*          chip label        hw-offset   con_id    idx  flags      */
        GPIO_LOOKUP("pinctrl-bcm2711", GPIO_PIN, "motion",  GPIO_ACTIVE_HIGH),
        { }   /* sentinel */
    },
};

/* ------------------------------------------------------------------ */
/*  Module-level state                                                  */
/* ------------------------------------------------------------------ */
static int            major_number;
static struct class  *motion_class  = NULL;
static struct device *motion_device = NULL;
static struct gpio_desc *gpio_desc  = NULL;
static int            irq_number;

/*
 * atomic_t: the IRQ handler (hard-IRQ context) and read()/poll()
 * (process context) must not race on a plain int without a lock.
 */
static atomic_t motion_detected = ATOMIC_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(motion_wait_queue);

/* ------------------------------------------------------------------ */
/*  IRQ handler                                                         */
/* ------------------------------------------------------------------ */
static irqreturn_t motion_irq_handler(int irq, void *dev_id)
{
    /* dev_id == gpio_desc (set in request_irq) */
    int gpio_value = gpiod_get_value(gpio_desc);

    if (gpio_value == 1) {
        atomic_set(&motion_detected, 1);
        wake_up_interruptible(&motion_wait_queue);
        pr_info("motion_gpio: Motion detected!\n");
    }
    return IRQ_HANDLED;
}

/* ------------------------------------------------------------------ */
/*  File operations                                                     */
/* ------------------------------------------------------------------ */
static int motion_open(struct inode *inode, struct file *file)
{
    pr_info("motion_gpio: Device opened\n");
    return 0;
}

static int motion_release(struct inode *inode, struct file *file)
{
    pr_info("motion_gpio: Device closed\n");
    return 0;
}

static ssize_t motion_read(struct file *file, char __user *buffer,
                           size_t len, loff_t *offset)
{
    char msg[2];

    if (len < 2)
        return -EINVAL;

    msg[0] = atomic_read(&motion_detected) ? '1' : '0';
    msg[1] = '\n';

    if (copy_to_user(buffer, msg, 2))
        return -EFAULT;

    atomic_set(&motion_detected, 0);   /* clear after read */
    return 2;
}

static __poll_t motion_poll(struct file *file, poll_table *wait)
{
    poll_wait(file, &motion_wait_queue, wait);

    if (atomic_read(&motion_detected))
        return EPOLLIN | EPOLLRDNORM;  /* renamed from POLLIN in kernel 4.16 */

    return 0;
}

static const struct file_operations fops = {
    .owner   = THIS_MODULE,
    .open    = motion_open,
    .release = motion_release,
    .read    = motion_read,
    .poll    = motion_poll,
};

/* ------------------------------------------------------------------ */
/*  Module init / exit                                                  */
/* ------------------------------------------------------------------ */
static int __init motion_gpio_init(void)
{
    int result;

    pr_info("motion_gpio: Initializing GPIO motion sensor driver\n");

    /* 1. Register character device */
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        pr_err("motion_gpio: Failed to register character device\n");
        return major_number;
    }

    /* 2. Create device class
     *    class_create() takes only (name) since kernel 6.4.           */
    motion_class = class_create(CLASS_NAME);
    if (IS_ERR(motion_class)) {
        result = PTR_ERR(motion_class);
        pr_err("motion_gpio: Failed to create class\n");
        goto err_chrdev;
    }

    /* 3. Create /dev/motion0
     *    The device name "motion0" must match motion_gpio_table.dev_id */
    motion_device = device_create(motion_class, NULL,
                                  MKDEV(major_number, 0),
                                  NULL, DEVICE_NAME "0");
    if (IS_ERR(motion_device)) {
        result = PTR_ERR(motion_device);
        pr_err("motion_gpio: Failed to create device\n");
        goto err_class;
    }

    /* 4. Register the lookup table BEFORE calling gpiod_get().
     *    This is what replaces gpiochip_find() + gpiochip_get_desc().
     *    The kernel's gpiod_get() will consult this table to resolve
     *    ("motion0", "motion") → (pinctrl-bcm2711, offset 17).        */
    gpiod_add_lookup_table(&motion_gpio_table);

    /* 5. Obtain GPIO descriptor via the lookup table */
    gpio_desc = gpiod_get(motion_device, "motion", GPIOD_IN);
    if (IS_ERR(gpio_desc)) {
        result = PTR_ERR(gpio_desc);
        pr_err("motion_gpio: GPIO get failed: %d\n", result);
        goto err_lookup;
    }

    /* 6. Map GPIO → IRQ */
    irq_number = gpiod_to_irq(gpio_desc);
    if (irq_number < 0) {
        result = irq_number;
        pr_err("motion_gpio: Failed to get IRQ: %d\n", result);
        goto err_gpiod;
    }

    /* 7. Register IRQ handler (rising edge = motion start)
     *    Pass gpio_desc as dev_id so free_irq() identifies this
     *    handler unambiguously (never pass NULL for shared lines).     */
    result = request_irq(irq_number, motion_irq_handler,
                         IRQF_TRIGGER_RISING,
                         "motion_gpio_irq",
                         gpio_desc);
    if (result) {
        pr_err("motion_gpio: Failed to request IRQ: %d\n", result);
        goto err_gpiod;
    }

    pr_info("motion_gpio: Driver initialized — /dev/motion0 ready\n");
    pr_info("motion_gpio: Monitoring GPIO%d (IRQ %d)\n",
            GPIO_PIN, irq_number);
    return 0;

    /* Unwind on failure */
err_gpiod:
    gpiod_put(gpio_desc);
err_lookup:
    gpiod_remove_lookup_table(&motion_gpio_table);
    device_destroy(motion_class, MKDEV(major_number, 0));
err_class:
    class_destroy(motion_class);
err_chrdev:
    unregister_chrdev(major_number, DEVICE_NAME);
    return result;
}

static void __exit motion_gpio_exit(void)
{
    free_irq(irq_number, gpio_desc);     /* dev_id must match request_irq */
    gpiod_put(gpio_desc);
    gpiod_remove_lookup_table(&motion_gpio_table);
    device_destroy(motion_class, MKDEV(major_number, 0));
    class_destroy(motion_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    pr_info("motion_gpio: Driver removed\n");
}

module_init(motion_gpio_init);
module_exit(motion_gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danny");
MODULE_DESCRIPTION("GPIO Motion Sensor Driver for HC-SR501");
MODULE_VERSION("1.2");
```
Creare Makefile:
```
nano Makefile
```
```makefile
obj-m += motion_gpio.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	sudo insmod motion_gpio.ko
	sudo chmod 666 /dev/motion0

uninstall:
	sudo rmmod motion_gpio
```
Compile and load:
```
# Compile
make

# Load driver
sudo insmod motion_gpio.ko

# Check if loaded
lsmod | grep motion_gpio
dmesg | tail -20

# You should see:
# motion_gpio: Driver initialized successfully
# motion_gpio: Device created at /dev/motion0
# motion_gpio: Monitoring GPIO17 (IRQ 49)

# Verify device exists
ls -l /dev/motion0
```
---
## (Not Tested)
### 2. Test Test GPIO Driver with Real Hardware (HC-SR501 PIR Motion Sensor)
**Wiring**:
```
HC-SR501 PIR          Raspberry Pi 4
─────────────────────────────────────────
VCC (Red wire)    →   Pin 2  (5V Power)
GND (Black wire)  →   Pin 6  (Ground)
OUT (Yellow wire) →   Pin 11 (GPIO17)
```
```
# Test 1: Read motion state
cat /dev/motion0
# Should show: 0 (no motion) or 1 (motion detected)

# Test 2: Monitor for motion events
while true; do
    state=$(cat /dev/motion0)
    if [ "$state" = "1" ]; then
        echo "🚨 Motion detected at $(date)"
    fi
    sleep 0.5
done

# Wave hand at PIR sensor - you should see detections!
```
**Expected:**
```
Motion: 0  14:23:15
Motion: 0  14:23:16
Motion: 1  14:23:17  ← Waved!
Motion: 1  14:23:17
Motion: 0  14:23:18
```
Kernel logs:
```
# In another SSH session, watch kernel logs live:
dmesg -w

# Then in first session, wave at PIR
# You should see:
# motion_gpio: Motion detected!
```
### 3. Test with our daemon (poll support)
```
# Your doorbellod daemon can now use epoll() on /dev/motion0!
# The driver's poll() function will wake epoll_wait() when motion occurs
```
---
## Part 2. I2C Distance Sensor (VL53L1X)
### 1. Wire VL53L1X Sensor
**Wiring:**
```
VL53L1X Pin     →    Raspberry Pi
─────────────────────────────────────
VCC (3.3V)      →    Pin 1 (3.3V)
GND             →    Pin 9 (Ground)
SDA             →    Pin 3 (GPIO2 - SDA)
SCL             →    Pin 5 (GPIO3 - SCL)
```
### 2. Verify I2C Detection
```
# Scan I2C bus
sudo i2cdetect -y 1

# Should show VL53L1X at address 0x29:
#      0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
# 00:                         -- -- -- -- -- -- -- -- 
# 10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
# 20: -- -- -- -- -- -- -- -- -- 29 -- -- -- -- -- -- 
# 30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
```
### 3. Test with i2c-tools
```
# Read sensor ID (should be 0xEA for VL53L1X)
sudo i2cget -y 1 0x29 0x00
# Output: 0xea

# Sensor detected successfully!
```
### 4. Create VL53L1X Driver (Optional - Advanced)
We can use userspace I2C access from our daemon:
```c
// In doorbellod.c:
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

int i2c_fd = open("/dev/i2c-1", O_RDWR);
ioctl(i2c_fd, I2C_SLAVE, 0x29);

// Read distance
uint8_t reg = 0x96;  // Distance register
uint16_t distance;
write(i2c_fd, &reg, 1);
read(i2c_fd, &distance, 2);

printf("Distance: %d mm\n", distance);
```
## Part 3. Camera Module Setup
### 1. Physical Connection
1. Power OFF Raspberry Pi
2. Locate CSI port (between HDMI and USB ports)
3. Pull up black plastic clip
4. Insert ribbon cable (blue side facing USB ports)
5. Push down clip to lock
6. Power ON
### 2. Install Camera Tools
```
sudo apt update
sudo apt install -y libcamera-apps libcamera-dev
```
### 3. Test Camera
```
# List cameras
libcamera-hello --list-cameras

# Should show:
# Available cameras
# -----------------
# 0 : imx219 [3280x2464] (/base/soc/i2c0mux/i2c@1/imx219@10)

# Take test photo
libcamera-still -o test.jpg --width 1280 --height 720

# Download to your Mac to verify
# (on Mac terminal)
scp pi@raspberrypi.local:~/test.jpg ~/Desktop/
```

### 4. Capture Video
```
# Record 10 second video
libcamera-vid -t 10000 -o test.h264 --width 1280 --height 720

# Convert to MP4 (install ffmpeg)
sudo apt install -y ffmpeg
ffmpeg -i test.h264 -c:v copy test.mp4

# Download and watch!
scp pi@raspberrypi.local:~/test.mp4 ~/Desktop/
```
## Part 4. Integration Test with Real Hardware
Test Script for All Sensors:
```
cat > ~/test_all_sensors.sh << 'ENDTEST'
#!/bin/bash

echo "================================"
echo "  Hardware Integration Test"
echo "================================"
echo ""

# Test 1: Motion Sensor
echo "[1/4] Testing Motion Sensor..."
if [ -c /dev/motion0 ]; then
    echo "  ✓ /dev/motion0 exists"
    state=$(timeout 1 cat /dev/motion0)
    echo "  ✓ Motion state: $state"
else
    echo "  ✗ Motion driver not loaded"
fi

echo ""

# Test 2: I2C Distance Sensor  
echo "[2/4] Testing I2C Distance Sensor..."
result=$(sudo i2cdetect -y 1 | grep "29")
if [ -n "$result" ]; then
    echo "  ✓ VL53L1X detected at 0x29"
else
    echo "  ✗ VL53L1X not detected"
fi

echo ""

# Test 3: Camera
echo "[3/4] Testing Camera..."
if command -v libcamera-hello &> /dev/null; then
    cameras=$(libcamera-hello --list-cameras 2>&1 | grep "Available cameras")
    if [ -n "$cameras" ]; then
        echo "  ✓ Camera detected"
    else
        echo "  ✗ Camera not connected"
    fi
else
    echo "  ✗ libcamera tools not installed"
fi

echo ""

# Test 4: SPI
echo "[4/4] Testing SPI..."
if ls /dev/spidev* &> /dev/null; then
    echo "  ✓ SPI devices available"
    ls /dev/spidev*
else
    echo "  ✗ SPI not enabled"
fi

echo ""
echo "================================"
echo "  Test Complete!"
echo "================================"
ENDTEST

chmod +x ~/test_all_sensors.sh
./test_all_sensors.sh
```
