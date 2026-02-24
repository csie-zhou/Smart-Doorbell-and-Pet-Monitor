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
Use the Lite version (no desktop) because it's faster and we only need the command line:
```
curl -L -o raspios-lite.img.xz "https://downloads.raspberrypi.org/raspios_lite_arm64/images/raspios_lite_arm64-2024-03-15/2024-03-15-raspios-bookworm-arm64-lite.img.xz"
```
It might take a few minutes. When done, extract it:
```
xz -d raspios-lite.img.xz
```

### Step 4: Resize the Image
The default image is too small for kernel development. Let's add 8GB of space:
```
qemu-img resize -f raw raspios-lite.img +8G
```
Expected output: `Image resized.`

### Step 5: Download Kernel and Device Tree
QEMU needs a kernel file and device tree to boot. Download these:
```
# Download kernel 
curl -L -o kernel8.img https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/kernel8.img

# Download device tree 
curl -L -o bcm2710-rpi-3-b-plus.dtb https://raw.githubusercontent.com/raspberrypi/firmware/master/boot/bcm2710-rpi-3-b-plus.dtb
```

### Step 6: Create Boot Script
Create a script to boot the VM easily:
```
nano start-qemu.sh
```
Paste this (optimized for M2):
```
qemu-system-aarch64 \
  -M raspi3b \
  -cpu cortex-a72 \
  -m 1G \
  -smp 4 \
  -kernel kernel8.img \
  -dtb bcm2710-rpi-3-b-plus.dtb \
  -drive if=sd,file=raspios-lite.img,format=raw \
  -append "earlycon=pl011,0x3f201000 console=ttyAMA0,115200 root=PARTUUID=fb33757d-02 rootfstype=ext4 fsck.repair=yes rootwait" \
  -serial mon:stdio \
  -nographic
```
```
#!/bin/bash
qemu-system-aarch64 \
  -M raspi3b \
  -cpu cortex-a72 \
  -m 1G \
  -smp 4 \
  -kernel kernel8.img \
  -dtb bcm2710-rpi-3-b-plus.dtb \
  -drive if=sd,file=raspios-lite.img,format=raw \
  -append "console=ttyAMA0,115200 root=PARTUUID=fb33757d-02 rootfstype=ext4 fsck.repair=yes rootwait" \
  -serial mon:stdio \
  -nographic
```
Save and exit. In nano, `Ctrl+O` (save), `Enter`, `Ctrl+X` (exit)
