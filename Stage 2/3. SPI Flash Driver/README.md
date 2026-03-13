# 3. SPI Flash Driver

## Goals

- Create SPI flash character device driver
- Implement read/write/ioctl operations
- Simulate flash memory in kernel
- Handle erase-before-write requirement
- Test from userspace with `dd` and `echo`

---

## Driver Architecture

Our driver will have these components:

1. **Character device interface:** `/dev/spiflash0`
2. **Simulated flash memory:** 1MB buffer in kernel
3. **`file_operations`:** read, write, llseek, ioctl
4. **ioctl commands:** `FLASH_ERASE_SECTOR`, `FLASH_GET_INFO`

---

## Step 1: Create Driver Structure
```
mkdir -p ~/kernel-modules/spiflash
cd ~/kernel-modules/spiflash
nano spiflash.c
```
```c
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/version.h>

#define DEVICE_NAME     "spiflash"
#define CLASS_NAME      "flash_class"
#define FLASH_SIZE      (1024 * 1024)
#define SECTOR_SIZE     4096

#define FLASH_IOCTL_MAGIC   'F'
#define FLASH_ERASE_SECTOR  _IOW(FLASH_IOCTL_MAGIC, 1, unsigned int)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("SPI Flash simulation driver");

static int            major_number;
static struct class  *flash_class;
static struct device *flash_device;
static char          *flash_memory;
static DEFINE_MUTEX(flash_mutex);

static int device_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t device_read(struct file *file, char __user *buf,
                            size_t len, loff_t *off)
{
    if (*off >= FLASH_SIZE)
        return 0;
    if (*off + len > FLASH_SIZE)
        len = FLASH_SIZE - *off;

    if (copy_to_user(buf, flash_memory + *off, len))
        return -EFAULT;

    *off += len;
    return len;
}

static ssize_t device_write(struct file *file, const char __user *buf,
                             size_t len, loff_t *off)
{
    size_t i;

    if (*off >= FLASH_SIZE)
        return -ENOSPC;
    if (*off + len > FLASH_SIZE)
        len = FLASH_SIZE - *off;

    /* Check erased */
    for (i = 0; i < len; i++) {
        if ((unsigned char)flash_memory[*off + i] != 0xFF) {
            printk(KERN_WARNING "Flash: not erased at %lld\n", *off + i);
            return -EINVAL;
        }
    }

    if (copy_from_user(flash_memory + *off, buf, len))
        return -EFAULT;

    *off += len;
    printk(KERN_INFO "Flash: wrote %zu bytes\n", len);
    return len;
}

static loff_t device_llseek(struct file *file, loff_t off, int whence)
{
    loff_t new_pos;
    switch (whence) {
    case SEEK_SET:                    // "go to exact position"
        new_pos = offset;
        break;
    
    case SEEK_CUR:                    // "move forward/back from current position"
        new_pos = file->f_pos + offset;
        break;
    
    case SEEK_END:                    // "go to end, then move"
        new_pos = FLASH_SIZE + offset;
        break;
    default:       return -EINVAL;
    }
    if (new_pos < 0 || new_pos > FLASH_SIZE)
        return -EINVAL;
    file->f_pos = new_pos;
    return new_pos;
}

static long device_ioctl(struct file *file, unsigned int cmd,
                          unsigned long arg)
{
    unsigned int sector;

    printk(KERN_INFO "Flash: ioctl cmd=0x%x ERASE=0x%lx\n",
           cmd, (unsigned long)FLASH_ERASE_SECTOR);

    if (cmd == FLASH_ERASE_SECTOR) {
        // arg = sector address to erase
        if (copy_from_user(&sector, (unsigned int __user *)arg,
                           sizeof(unsigned int)))
            return -EFAULT;
        sector = (sector / SECTOR_SIZE) * SECTOR_SIZE;
        if (sector >= FLASH_SIZE)
            return -EINVAL;
        mutex_lock(&flash_mutex);
        memset(flash_memory + sector, 0xFF, SECTOR_SIZE);
        mutex_unlock(&flash_mutex);
        printk(KERN_INFO "Flash: erased sector 0x%x\n", sector);
        return 0;
    }
    return -ENOTTY;
}

static struct file_operations fops = {
    .owner          = THIS_MODULE,
    .open           = device_open,
    .release        = device_release,
    .read           = device_read,
    .write          = device_write,
    .llseek         = device_llseek,
    .unlocked_ioctl = device_ioctl,  // 64-bit userspace
    .compat_ioctl   = device_ioctl,  // 32-bit userspace
};

static int __init spiflash_init(void)
{
    flash_memory = kmalloc(FLASH_SIZE, GFP_KERNEL);
    if (!flash_memory)
        return -ENOMEM;
    memset(flash_memory, 0xFF, FLASH_SIZE);

    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        kfree(flash_memory);
        return major_number;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    flash_class = class_create(CLASS_NAME);
#else
    flash_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(flash_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(flash_memory);
        return PTR_ERR(flash_class);
    }

    flash_device = device_create(flash_class, NULL,
                                 MKDEV(major_number, 0),
                                 NULL, DEVICE_NAME "0");
    if (IS_ERR(flash_device)) {
        class_destroy(flash_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(flash_memory);
        return PTR_ERR(flash_device);
    }

    printk(KERN_INFO "Flash: loaded, major=%d\n", major_number);
    return 0;
}

static void __exit spiflash_exit(void)
{
    device_destroy(flash_class, MKDEV(major_number, 0));
    class_destroy(flash_class);
    unregister_chrdev(major_number, DEVICE_NAME);
    kfree(flash_memory);
    printk(KERN_INFO "Flash: unloaded\n");
}

module_init(spiflash_init);
module_exit(spiflash_exit);
```
- `_IOW(FLASH_IOCTL_MAGIC, 1, uint32_t)`:
  - `IOW`: Write, userspace writes data to kernel (send sector address)
  - `FLASH_IOCTL_MAGIC`: Unique identifier for THIS driver
  - `1`: command number
  - `uint32_t`: data type (size) for the command `IOW`
- `device_llseek()`: controls where to read/write the device
- `device_ioctl()`: out-of-band control commands *(for userspace flash_erase command here)*
  - read(): get data from device
  - write(): send data to device
  - ioctl(): send **Commnands** to device (eg. erase sector, get flash info, set write protection)

---

## Step 2: Create Makefile
```
nano Makefile
```
```makefile
obj-m += spiflash.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

---

## Step 3: Compile and Load
```
# Compile
make

# Load
sudo insmod spiflash.ko

# Check dmesg
dmesg | tail -5
# Expected:
#   Flash: Initializing SPI flash driver
#   Flash: Device created (major 245)
#   Flash: Simulated 1024 KB flash memory

# Verify device exists
ls -l /dev/spiflash0
```

---

## Step 4: Create Userspace Test Tool
```
cd ~
nano flash_erase.c
```
```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/ioctl.h>

#define FLASH_IOCTL_MAGIC   'F'
#define FLASH_ERASE_SECTOR  _IOW(FLASH_IOCTL_MAGIC, 1, uint32_t)

int main(int argc, char *argv[])
{
    int      fd;
    uint32_t addr;

    if (argc != 2) {
        printf("Usage: %s <address>\n", argv[0]);
        return 1;
    }

    addr = (uint32_t)strtoul(argv[1], NULL, 0);

    fd = open("/dev/spiflash0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Erasing sector at 0x%x...\n", addr);

    if (ioctl(fd, FLASH_ERASE_SECTOR, &addr) < 0) {
        perror("ioctl");
        close(fd);
        return 1;
    }

    printf("Sector erased successfully\n");
    close(fd);
    return 0;
}
```
```
gcc -o flash_erase flash_erase.c
```

---

## Step 5: Test the Driver

### Test 1: Erase a Sector
```
# 1. Fresh driver
sudo rmmod spiflash
sudo insmod spiflash.ko
sudo chmod 666 /dev/spiflash0

# 2. Write
echo "Hello Flash" | sudo dd of=/dev/spiflash0 bs=1 count=12

# 3. Read back
sudo dd if=/dev/spiflash0 bs=1 count=12 2>/dev/null

# 4. Try overwrite — should FAIL
echo "Hello Flash" | sudo dd of=/dev/spiflash0 bs=1 count=12
dmesg | tail -3
# Flash: not erased at 0

# 5. Erase sector
sudo ./flash_erase 0x0000

# 6. Write again — should SUCCEED
echo 'New Data!!' | sudo dd of=/dev/spiflash0 bs=1 count=10
sudo dd if=/dev/spiflash0 bs=1 count=10 2>/dev/null
# New Data!!
```

---

## What To Learn

- **Flash driver architecture:** Character device + simulated storage
- **ioctl interface:** `FLASH_ERASE_SECTOR`, `FLASH_GET_INFO`
- **Flash constraints:** Must erase (set to `0xFF`) before writing
- **Mutex locking:** Protect shared resource from race conditions
- **`llseek` implementation:** Random access within flash
