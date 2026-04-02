# Initial Setup
## 1. Flash Raspberry Pi OS
```
# On Mac M2:
# Download from: https://www.raspberrypi.com/software/

# Or use Homebrew:
brew install --cask raspberry-pi-imager
```

#### **Flash the SD Card:**

1. Insert your microSD card into Mac
2. Open Raspberry Pi Imager
3. Choose:
   - **OS:** Raspberry Pi OS (64-bit) - recommended
   - **Storage:** Your SD card
4. Click ⚙️ (Settings) and configure:
```
   ✅ Enable SSH
   ✅ Set username: pi
   ✅ Set password: raspberry
   ✅ Configure WiFi (your network)
   ✅ Set locale: Taiwan/Taipei
```

## 2. Boot Raspberry Pi 
1. Insert SD card into Raspberry Pi
2. Connect power (USB-C for Pi 4)
3. Wait 30 seconds for boot
4. Find Pi's IP address:
```
# On Mac, find the Pi:
ping raspberrypi.local
```

## 3. First SSH Connection
```
ssh pi@raspberrypi.local
```
You should see `pi@raspberrypi:~ $`.

## 4. Initial Configuration
```
# Update system
sudo apt update
sudo apt upgrade -y

# Install essential tools
sudo apt install -y \
    raspberrypi-kernel-headers \
    build-essential \
    git \
    i2c-tools \
    spi-tools \
    v4l-utils \
    alsa-utils \
    vim \
    htop

# Check kernel version
uname -r
# Should be: 6.1.x or newer
```

## 5. Enable Hardware Interfaces
```
# Enable I2C, SPI, I2S, Camera
sudo raspi-config
```

**Navigate:**
```
3 Interface Options
  → I1 SSH: Enable (should already be on)
  → I3 VNC: Optional
  → I4 SPI: Enable
  → I5 I2C: Enable

Finish → Reboot: Yes
```

## 6. Verify
After reboot:
```
# 1. Check I2C
ls -l /dev/i2c*

# 2. Check SPI  
ls -l /dev/spidev*

# 3. Check GPIO chip exists
ls -l /dev/gpiochip*
```
