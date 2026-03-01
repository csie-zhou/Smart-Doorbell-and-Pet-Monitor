# 6. Character Device Driver

## Goals

- Understand character devices vs block devices
- Implement `file_operations` structure
- Register a character device with major/minor numbers
- Use `copy_to_user` and `copy_from_user`
- Create `/dev/motion0` with udev
- Test from userspace (`cat`, `echo`)

---

## Character vs Block Devices

| Aspect    | Character Device                          | Block Device                  |
|-----------|-------------------------------------------|-------------------------------|
| Access    | Stream of bytes                           | Fixed-size blocks             |
| Buffering | No buffering                              | Kernel buffering              |
| Examples  | Sensors, serial ports, `/dev/random`      | Hard drives, SD cards         |
| Seeking   | Often not supported                       | Random access                 |

Our motion sensor is a character device — it produces a stream of motion events, no seeking needed.

---

## Step 1: Understanding the Code Structure

A character device driver has these key components:

1. **`file_operations` structure** — Functions for open, read, write, release
2. **Device registration** — Get major/minor numbers
3. **Class creation** — For udev to auto-create `/dev/` node
4. **Data transfer** — `copy_to_user` / `copy_from_user`

---

## Step 2: Create the Driver

Create a new directory:
```
mkdir -p ~/kernel-modules/motion_char
cd ~/kernel-modules/motion_char
nano motion_char.c
```

Here's the driver code:
```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "motion"
#define CLASS_NAME "motion_class"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Danny");
MODULE_DESCRIPTION("Motion sensor character device");

static int major_number;
static struct class *motion_class = NULL;
static struct device *motion_device = NULL;

/* Simulated motion state (0 or 1) */
static int motion_state = 0;

// 1: Implement device_open
static int device_open(struct inode *inode, struct file *file)
{
	// Print a message that device was opened 
	printk(KERN_INFO "Motion device opened\n");
	return 0;
}

// 2: Implement device_read
static ssize_t device_read(struct file *file, char __user *buffer, size_t len, loff_t *offset)
{
	char msg[32];
	int msg_len;


	// Format motion state as string
	msg_len = snprintf(msg, sizeof(msg), "motion=%d\n", motion_state);

	// Avoid reading twice (check offset)
	if (*offset >= msg_len)
		return 0;

	// Copy data to userspace
	// copy_to_user(destination, source, size)
	if (copy_to_user(buffer, msg, msg_len)) {
		return -EFAULT;
	}
	
	*offset += msg_len;
	return msg_len;
}

// 3. Implement device_write
static ssize_t device_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset)
{
	char user_msg[32];
	
	if (len >= sizeof(user_msg))
		len = sizeof(user_msg) - 1;

	// Copy data from userspace
	// copy_from_user(destination, source, size)
	if (copy_from_user(user_msg, buffer, len)) {
		return -EFAULT;
	}
	
	user_msg[len] = '\0';

	// Simple parsing: "0" or "1"
	if (user_msg[0] == '1')
		motion_state = 1;
	else if (user_msg[0] == '0')
		motion_state = 0;

	printk(KERN_INFO "Motion state set to %d\n", motion_state);
	return len; 
}

static int device_release(struct inode *inode, struct file *file)
{
	// Print a message that device was closed
	printk(KERN_INFO "Motion device closed\n");
	return 0;
}

static struct file_operations fops = {
	.owner	= THIS_MODULE,
	.open 	= device_open,
	.read	= device_read,
	.write 	= device_write,
	.release= device_release,
};

static int __init motion_char_init(void) {
	printk(KERN_INFO "Motion: Initializing device\n");
	
	// Register character device
	major_number = register_chrdev(0, DEVICE_NAME, &fops);
	if (major_number < 0) {
		printk(KERN_ALERT "Motion: Failed to register\n");
		return major_number;
	}
	printk(KERN_INFO "Motion: Registered with major number %d\n", major_number);

	// Create device class
	motion_class = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(motion_class)) {
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Motion: Failed to create class\n");
		return PTR_ERR(motion_class);
	}

	// Create device
	motion_device = device_create(motion_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME "0");
	if (IS_ERR(motion_device)) {
		class_destroy(motion_class);
		unregister_chrdev(major_number, DEVICE_NAME);
		printk(KERN_ALERT "Motion: Failed to create device\n");
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

---

## Step 3: Create Makefile
```
nano Makefile
```
```makefile
obj-m += motion_char.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

---

## Step 4: Compile and Load

Compile:
```
make
```

Load the module:
```
sudo insmod motion_char.ko
```

Check dmesg:
```
dmesg | tail -10
```

Expected output:
```
[ 123.456] Motion: Initializing device
[ 123.457] Motion: Registered with major number 244
[ 123.458] Motion: Device created successfully
```

Check if `/dev/motion0` was created:
```
ls -l /dev/motion0
```

You should see:
```
crw------- 1 root root 244, 0 Mar 1 10:30 /dev/motion0
```

(/dev/motion0 is created at, `DEVICE_NAME "0"` is C string literal concatenation):

> **Note:** `c` means character device, `244` is the major number, `0` is the minor number.

```c
// Create device
motion_device = device_create(motion_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME "0");
```

---

## Step 5: Test from Userspace

### Read the Motion State
```
sudo cat /dev/motion0
```

Output:
```
motion=0
```

Check dmesg:
```
dmesg | tail -5
```

You should see:
```
[ 234.567] Motion device opened
[ 234.568] Motion device closed
```

### Write to Change Motion State

Set motion to `1` (detected):
```
echo '1' | sudo tee /dev/motion0
```

Check dmesg:
```
dmesg | tail -3
```

You should see:
```
[ 345.678] Motion device opened
[ 345.679] Motion state set to 1
[ 345.680] Motion device closed
```

Read it back:
```
sudo cat /dev/motion0
```

Output:
```
motion=1
```

**Success!** Communicating between userspace and kernel!

#### Tips
```
echo '1' | sudo tee /dev/motion0
```
Left side as **stdout**, shows `1` on the terminal. Right side `tee` works as a T-shaped pipe junction, **stdin** the `1` 
to the driver in the kernel.   
The middle `|` works as pipe, takes the **stdout of the left command** and feeds it as **stdin of the right command**.  

Without it, the `>` redirect would also write to `/dev/motion0`, but it runs as user, not root:
```
echo '1' > /dev/motion0
# Permission denied — /dev/motion0 is owned by root
```
```
sudo echo '1' > /dev/motion0
# Still fails! sudo elevates echo, but > redirect runs as your user
```

---

## Understanding Major/Minor Numbers

| Number | Purpose                                                              |
|--------|----------------------------------------------------------------------|
| Major  | Identifies the driver (e.g., `244` = motion driver)                 |
| Minor  | Identifies the specific device instance (`0` = first motion sensor) |

Example: If you had multiple motion sensors, they'd all share the same major (`244`) but use different minors (`0`, `1`, `2`...).

---

## What To Learn

- **Character device creation:** `register_chrdev`, `class_create`, `device_create`
- **`file_operations`:** open, read, write, release callbacks
- **User/kernel boundary:** `copy_to_user`, `copy_from_user` for safe data transfer
- **Device nodes:** `/dev/motion0` auto-created by udev
- **Userspace interaction:** `cat`/`echo` work because they use `read()`/`write()` syscalls

---

## Stage 1 Complete! 🎉

- ✅ Set up QEMU kernel development environment
- ✅ Written and loaded kernel modules
- ✅ Mastered I2C subsystem and i2c-tools
- ✅ Created a character device driver
- ✅ Implemented kernel-userspace communication

