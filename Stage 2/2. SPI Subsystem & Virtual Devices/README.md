# 2. SPI Subsystem & Virtual Devices

## Goals

- Understand SPI protocol (MOSI, MISO, SCK, CS)
- Learn SPI modes (CPOL/CPHA)
- Enable SPI interface in Linux
- Use `spidev` for userspace SPI access
- Test SPI communication with `spidev_test`
- Understand flash memory commands

---

## SPI vs I2C Comparison

| Aspect        | I2C                      | SPI                          |
|---------------|--------------------------|------------------------------|
| Wires         | 2 (SDA, SCL)             | 4+ (MOSI, MISO, SCK, CS)     |
| Speed         | ~400 kHz (standard)      | 10–100 MHz                   |
| Communication | Half-duplex              | Full-duplex                  |
| Addressing    | 7-bit address            | Chip Select pin              |
| Best for      | Sensors, low-speed       | Flash, displays, high-speed  |

---

## SPI Protocol Basics

### The 4 SPI Signals

| Signal | Full Name              | Purpose                              |
|--------|------------------------|--------------------------------------|
| MOSI   | Master Out Slave In    | Data from master to slave            |
| MISO   | Master In Slave Out    | Data from slave to master            |
| SCK    | Serial Clock           | Clock signal from master             |
| CS/SS  | Chip Select/Slave Select | Activates specific device (active LOW) |

> **Key Point:** MOSI and MISO operate simultaneously — data goes both directions on every clock cycle. This is what makes SPI **full-duplex**.

---

## SPI Modes (CPOL/CPHA)

SPI has 4 modes defined by Clock Polarity (CPOL) and Clock Phase (CPHA):

| Mode | CPOL | CPHA | Description                              |
|------|------|------|------------------------------------------|
| 0    | 0    | 0    | Clock idle LOW, data on rising edge      |
| 1    | 0    | 1    | Clock idle LOW, data on falling edge     |
| 2    | 1    | 0    | Clock idle HIGH, data on falling edge    |
| 3    | 1    | 1    | Clock idle HIGH, data on rising edge     |

> **Most common:** Mode 0 (CPOL=0, CPHA=0). Mode **must match** between master and slave!

---

## Step 1: Enable SPI in QEMU

SSH into your VM:
```
ssh pi@localhost -p 5022
```

Check if SPI is enabled:
```
ls /dev/spi*
```

Expected: You'll likely see `No such file or directory` — SPI isn't enabled by default in QEMU.

Load SPI driver:
```
sudo modprobe spi-bcm2835
sudo modprobe spidev
```
`spi-bcm2835` might not be enabled as default, check the current SPI status:
```
sudo raspi-config nonint get_spi
# Returns 0 = enabled, 1 = disabled
```

Enable it:
```
sudo raspi-config nonint do_spi 0
```

Check again:
```
ls /dev/spi*
```

> **Note:** In QEMU, you may not see `/dev/spidev*` devices unless hardware is properly configured. This is expected — the important part is understanding SPI concepts and the driver structure you'll implement.

---

## Step 2: Understanding Linux SPI Architecture

Linux SPI subsystem has 3 layers:

| Layer               | Description                                              |
|---------------------|----------------------------------------------------------|
| SPI Core            | Generic SPI framework in kernel                          |
| SPI Controller      | Hardware controller driver (`spi-bcm2835` on RPi)        |
| SPI Protocol Driver | Device-specific driver (flash, display, etc) OR `spidev` for userspace |

---

## Step 3: Flash Memory Commands

SPI flash chips like W25Q128 use standard JEDEC commands:

| Command       | Opcode (hex) | Purpose                          |
|---------------|--------------|----------------------------------|
| READ_DATA     | `0x03`       | Read data from address           |
| PAGE_PROGRAM  | `0x02`       | Write up to 256 bytes            |
| SECTOR_ERASE  | `0x20`       | Erase 4KB sector                 |
| WRITE_ENABLE  | `0x06`       | Enable write operations          |
| READ_STATUS   | `0x05`       | Check if busy (bit 0)            |
| JEDEC_ID      | `0x9F`       | Read chip ID (mfr + device)      |

> **Important:** Flash must be erased (all `0xFF`) before writing!

---

## What Learned

- **SPI protocol:** 4-wire (MOSI, MISO, SCK, CS), full-duplex
- **SPI modes:** CPOL/CPHA timing variations
- **Linux SPI:** Core, controller, protocol driver layers
- **Flash commands:** READ, WRITE, ERASE operations
- **Speed advantage:** 10MHz+ vs I2C's 400kHz
