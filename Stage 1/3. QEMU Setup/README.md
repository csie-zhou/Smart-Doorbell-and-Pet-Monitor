# QEMU Setup

## Overview
- Install QEMU on Mac
- Boot Raspberry Pi OS in a virtual machine
-	SSH into the VM from your Mac Terminal
-	Install kernel headers and build tools
-	Understand the kernel development workflow

--- 

### Step 1: Install Homebrew
#### Check if Homebrew is installed
```
brew --version
```
If you see a version number: Great! Skip to Step 2.  
If you see 'command not found': Continue below.
#### Install Homebrew
```
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```
For Apple Silicon, add Homebrew to your PATH.
```
echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile eval "$(/opt/homebrew/bin/brew shellenv)"
```
Verify if worked.
```
brew --version
```
### Step 2: Install QEMU
```
brew install qemu
```
Verify installation:
```
qemu-system-aarch64 --version
```
Should see something like `QEMU emulator version 8.x.x`.


### Step 3: Download Raspberry Pi OS
#### Create a Working Directory
```
mkdir -p ~/qemu-rpi
cd ~/qemu-rpi
```
#### Download Raspberry Pi OS Lite (64-bit)

Use the Bullseye Lite version (no desktop) — Bookworm requires an initramfs that QEMU cannot load, so Bullseye is required for QEMU compatibility:
```
curl -L -o bullseye-lite.img.xz \
"https://downloads.raspberrypi.org/raspios_lite_arm64/images/raspios_lite_arm64-2023-05-03/2023-05-03-raspios-bullseye-arm64-lite.img.xz"
```

It might take a few minutes. When done, extract it:
```
xz -d bullseye-lite.img.xz
```

### Step 4: Resize the Image

The default image is too small for kernel development. Let's add 8GB of space:
```
qemu-img resize -f raw bullseye-lite.img +8G
```

Expected output: `Image resized.`

### Step 5: Extract Kernel and Device Tree

Extract the kernel and device tree directly from the image itself to guarantee they match:
```
hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount bullseye-lite.img
mkdir -p /tmp/rpi-boot
sudo mount_msdos /dev/disk8s1 /tmp/rpi-boot
cp /tmp/rpi-boot/kernel8.img ./kernel8-bullseye.img
cp /tmp/rpi-boot/bcm2710-rpi-3-b-plus.dtb ./bcm2710-bullseye.dtb
sudo umount /tmp/rpi-boot
hdiutil detach /dev/disk8s1
file kernel8-bullseye.img
file bcm2710-bullseye.dtb
```

### Step 6: Create User Credentials

Newer Raspberry Pi OS has no default user. Create one before first boot:
```
hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount bullseye-lite.img
sudo mount_msdos /dev/disk8s1 /tmp/rpi-boot
touch /tmp/rpi-boot/ssh
HASH=$(openssl passwd -6 raspberry)
echo "pi:$HASH" | sudo tee /tmp/rpi-boot/userconf.txt
sudo umount /tmp/rpi-boot
hdiutil detach /dev/disk8s1
```

### Step 7: Create Boot Script
```
nano start-qemu.sh
```

Paste this:

```bash
#!/bin/bash
qemu-system-aarch64 \
  -M raspi3b \
  -cpu cortex-a72 \
  -m 1G \
  -smp 4 \
  -kernel kernel8-bullseye.img \
  -dtb bcm2710-bullseye.dtb \
  -drive if=sd,file=bullseye-lite.img,format=raw \
  -append "earlycon=pl011,0x3f201000 console=ttyAMA0,115200 root=/dev/mmcblk0p2 rootfstype=ext4 fsck.repair=yes rootwait rootdelay=2" \
  -serial mon:stdio \
  -nographic \
  -netdev user,id=net0,hostfwd=tcp::5022-:22 \
  -device usb-net,netdev=net0
```

Save and exit: `Ctrl+O`, `Enter`, `Ctrl+X`. Then:
```
chmod +x start-qemu.sh
```

### Step 8: First Boot
```
./start-qemu.sh
```
Wait for the login prompt: `raspberrypi login:`. Use default:  
  - Username: pi
  - Password: raspberry

### Step 9: Initial Configuration
#### Expand Filesystem

Tell Raspberry Pi to use all that resized image space:
```
sudo raspi-config --expand-rootfs
```

Then reboot:
```
sudo reboot
```

#### Enable SSH

Enable SSH so we can connect from Mac Terminal:
```
sudo systemctl enable ssh
sudo systemctl start ssh
```

Verify SSH is running:
```
sudo systemctl status ssh
```

Look for `Active: active (running)` in green.

### Step 10: SSH from Mac Terminal

Open a **NEW Terminal window** (⌘+N) and run:
```
ssh pi@localhost -p 5022
```

Type `yes` and press Enter.

Password: `raspberry`

#### Optional: Create SSH Alias

Add this to Mac's `~/.ssh/config`:
```
nano ~/.ssh/config
```

Add these lines:
```
Host qemu-rpi
    HostName localhost
    Port 5022
    User pi
```

Now we can connect with just:
```
ssh qemu-rpi
```

### Step 11: Install Kernel Headers

#### Update Package Lists
```
sudo apt update
```

#### Install Development Tools
```
sudo apt install -y raspberrypi-kernel-headers build-essential git vim i2c-tools
```

> If `raspberrypi-kernel-headers` fails due to a connection reset, retry with:
> ```
> sudo apt install -y raspberrypi-kernel-headers --fix-missing
> ```

This installs:
- `raspberrypi-kernel-headers` — needed to compile kernel modules
- `build-essential` — gcc, make, and other build tools
- `git` — version control
- `vim` — text editor
- `i2c-tools` — for testing I2C devices


### Step 12: Verify Installation

#### Check Kernel Version
```
uname -r
```

You should see something like: `6.1.21+rpt-rpi-v8`

#### Verify Kernel Headers
```
ls /lib/modules/$(uname -r)/build
```

You should see files like `Makefile`, `Kconfig`, etc.

#### Verify gcc
```
gcc --version
```

Should show gcc version 10.x or higher.

#### Verify i2c-tools
```
i2cdetect -V
```

Should show version information.

### Troubleshooting

**QEMU won't start**
- Make sure all files are in your project directory
- Check: `ls -lh kernel8-bullseye.img bcm2710-bullseye.dtb bullseye-lite.img`
- Try: `qemu-system-aarch64 --version` to verify QEMU works

**Stuck at boot (no login prompt)**
- Wait 3-5 minutes — first boot is slow
- If still stuck after 5 min, press `Ctrl+A` then `X` to exit and retry

**SSH connection refused**
- In QEMU console: `sudo systemctl status ssh`
- If not running: `sudo systemctl start ssh`
- Check port forwarding in start script: `hostfwd=tcp::5022-:22`

**Kernel headers not found**
- Run: `sudo apt update && sudo apt install raspberrypi-kernel-headers --fix-missing`
- Verify: `ls /lib/modules/$(uname -r)/build`

### Useful Commands Reference

| Command | Purpose |
|---|---|
| `./start-qemu.sh` | Start the VM |
| `Ctrl+A`, then `X` | Exit QEMU console |
| `ssh pi@localhost -p 5022` | Connect via SSH from Mac |
| `sudo shutdown -h now` | Properly shut down VM |
| `uname -a` | Show kernel info |
| `df -h` | Show disk space |


