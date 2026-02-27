# 4. Hello World Kernel Module
## Goals
- Understand kernel vs userspace
- Write a minimal kernel module
- Create a Makefile for kernel compilation
- Load/unload modules with `insmod`/`rmmod`
- Debug with `dmesg`



## Kernel vs Userspace: Key Differences

| Aspect       | Userspace                  | Kernel Space                        |
|--------------|----------------------------|--------------------------------------|
| Privileges   | Limited (protected)        | Full hardware access                 |
| Libraries    | libc, printf, malloc       | printk, kmalloc (**NO libc**)        |
| Crashes      | Process dies, system OK    | **KERNEL PANIC** (whole system)      |
| Entry point  | `main()` function          | `module_init()` macro                |



## Step 1: Create Working Directory
SSH into VM:
```bash
ssh pi@localhost -p 5022
```
or 
```bash
ssh qemu-rpi
```

Create a directory for kernel modules:
```bash
mkdir -p ~/kernel-modules/hello
cd ~/kernel-modules/hello
```

## Step 2: Write the Hello Module

Create the file:
```bash
nano hello.c
```
```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

// CHALLENGE 1: Define MODULE_LICENSE
// Hint: Use "GPL" for open source
MODULE_LICENSE("GPL");

// CHALLENGE 2: Add MODULE_AUTHOR with your name
MODULE_AUTHOR("Danny");

// CHALLENGE 3: Add MODULE_DESCRIPTION
MODULE_DESCRIPTION("Tutorial kernel module.");

// CHALLENGE 4: Write the init function
// Hint: Use printk with KERN_INFO log level
static int __init hello_init(void) {
    // Print: "Hello from kernel space!"
    printk(KERN_INFO "Hello from kernel space!\n");
    return 0;  // 0 = success
}

// CHALLENGE 5: Write the exit function
static void __exit hello_exit(void) {
    // Print: "Goodbye from kernel space!"
    printk(KERN_INFO "Goodbye from kernel space!\n");
}

// CHALLENGE 6: Register init and exit functions
// Hint: Use module_init() and module_exit() macros
module_init(hello_init);
module_exit(hello_exit);
```

## Step 3: Create the Makefile
The kernel has its own build system (Kbuild). Create a Makefile:
```bash
nano Makefile
```
Use **TABs**, not spaces, for indentation under `all:` and `clean:`
```makefile
obj-m += hello.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

### Understanding the Makefile

| Line                        | Meaning                                                        |
|-----------------------------|----------------------------------------------------------------|
| `obj-m += hello.o`          | Tell Kbuild to compile `hello.c` into a module (`.ko` file)   |
| `uname -r`                  | Get current kernel version (e.g., `6.1.0-rpi7-rpi-v8`)        |
| `/lib/modules/.../build`    | Kernel headers directory (installed on Day 1-2)                |
| `M=$(PWD)`                  | Tell Kbuild where our module source is (current directory)     |

## Step 4: Compile the Module

Build it:
```bash
make
```

If successful, you'll have a `hello.ko` file:
```bash
ls -lh hello.ko
```

## Step 5: Load the Module

Load your module into the running kernel:
```bash
sudo insmod hello.ko
```

### Check the Kernel Log
```bash
dmesg | tail -10
```

You should see:
```
[ 123.456789] Hello from kernel space!
```

### Verify Module is Loaded
```bash
lsmod | grep hello
```

You should see:
```
hello    16384    0
```

This shows: module name, size in bytes, reference count (`0` = not in use).

## Step 6: Unload the Module

Remove the module from the kernel:
```bash
sudo rmmod hello
```

Check the kernel log again:
```bash
dmesg | tail -10
```

You should now see:
```
[ 123.456789] Hello from kernel space!
[ 234.567890] Goodbye from kernel space!
```

The exit function ran! Verify it's gone:
```bash
lsmod | grep hello
```

Should return nothing (module is unloaded).

## Step 7: View Module Info

Check the metadata you added:
```bash
modinfo hello.ko
```

You'll see:
```
filename:    /home/pi/kernel-modules/hello/hello.ko
description: Tutorial kernel module.
author:      Danny
license:     GPL
...
```

## What to Learn

- **Kernel module structure:** init, exit, macros
- **`printk` vs `printf`:** Kernel logging with log levels
- **Kbuild system:** Kernel Makefiles differ from userspace
- **Module lifecycle:** `insmod` → `module_init` → *(running)* → `rmmod` → `module_exit`
- **Debugging tools:** `dmesg`, `lsmod`, `modinfo`


