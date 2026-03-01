# 5. Virtual I2C Bus Setup
### Testing I2C Drivers Without Physical Hardware

## Goals

- Understand Linux I2C subsystem architecture
- Load `i2c-stub` to create a virtual I2C bus
- Create fake devices at specific addresses
- Use `i2c-tools` (`i2cdetect`, `i2cget`, `i2cset`)
- Explore sysfs I2C hierarchy

## I2C Subsystem Architecture

Linux's I2C subsystem has three layers:

| Layer        | Description                                                              |
|--------------|--------------------------------------------------------------------------|
| I2C Core     | Generic I2C framework code in kernel                                     |
| I2C Adapter  | Bus controller driver (e.g., `i2c-bcm2835` on RPi, `i2c-stub` for testing) |
| I2C Client   | Device driver (what we'll write for sensors)                            |


## Step 0: Compile i2c-stub Module of our own

Identify if the `i2c-stub` module exists:
```bash
find /lib/modules/$(uname -r) -name "i2c-stub*"
```

If shows nothing, then `i2c-stub` module isn't included in the kernel, which is common on stripped-down Pi kernel. We manually write it:
```bash
mkdir -p ~/kernel-modules/i2c-stub
cd ~/kernel-modules/i2c-stub
nano i2c-stub.c
```

Paste this simplified version:
```C
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#define MAX_CHIPS 10
#define STUB_FUNC (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE | \
                   I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA | \
                   I2C_FUNC_SMBUS_BLOCK_DATA)

static unsigned short chip_addr[MAX_CHIPS];
static int num_chips;
module_param_array(chip_addr, ushort, &num_chips, S_IRUGO);
MODULE_PARM_DESC(chip_addr, "Chip addresses (up to 10, e.g. chip_addr=0x50,0x29)");

/* One register bank per chip */
static u8 registers[MAX_CHIPS][256];

/* Track registered client devices */
static struct i2c_client *stub_clients[MAX_CHIPS];

static s32 stub_xfer(struct i2c_adapter *adap, u16 addr,
                     unsigned short flags, char read_write,
                     u8 command, int size, union i2c_smbus_data *data)
{
    int i;
    int chip_index = -1;

    for (i = 0; i < num_chips; i++) {
        if (chip_addr[i] == addr) {
            chip_index = i;
            break;
        }
    }

    if (chip_index < 0)
        return -ENODEV;

    switch (size) {
    case I2C_SMBUS_QUICK:
        break;
    case I2C_SMBUS_BYTE:
        if (read_write == I2C_SMBUS_WRITE)
            registers[chip_index][command] = 0;
        else
            data->byte = registers[chip_index][command];
        break;
    case I2C_SMBUS_BYTE_DATA:
        if (read_write == I2C_SMBUS_WRITE)
            registers[chip_index][command] = data->byte;
        else
            data->byte = registers[chip_index][command];
        break;
    case I2C_SMBUS_WORD_DATA:
        if (read_write == I2C_SMBUS_WRITE) {
            registers[chip_index][command]     = data->word & 0xff;
            registers[chip_index][command + 1] = data->word >> 8;
        } else {
            data->word = registers[chip_index][command] |
                        (registers[chip_index][command + 1] << 8);
        }
        break;
    default:
        return -EOPNOTSUPP;
    }
    return 0;
}

static u32 stub_func(struct i2c_adapter *adapter)
{
    return STUB_FUNC;
}

static const struct i2c_algorithm smbus_algorithm = {
    .smbus_xfer    = stub_xfer,
    .functionality = stub_func,
};

static struct i2c_adapter stub_adapter = {
    .owner   = THIS_MODULE,
    .class   = I2C_CLASS_HWMON | I2C_CLASS_SPD,
    .algo    = &smbus_algorithm,
    .name    = "SMBus stub driver",
};

/* Dummy driver so clients can bind */
static const struct i2c_device_id stub_device_id[] = {
    { "stub", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, stub_device_id);

static int stub_probe(struct i2c_client *client)
{
    pr_info("i2c-stub: device probed at 0x%02x\n", client->addr);
    return 0;
}

static void stub_remove(struct i2c_client *client)
{
    pr_info("i2c-stub: device removed at 0x%02x\n", client->addr);
}

static struct i2c_driver stub_driver = {
    .driver = {
        .name = "stub",
    },
    .probe_new  = stub_probe,
    .remove     = stub_remove,
    .id_table   = stub_device_id,
};

static int __init i2c_stub_init(void)
{
    int i, ret;

    if (!num_chips) {
        pr_err("i2c-stub: no chip addresses specified\n");
        return -EINVAL;
    }

    /* Register the adapter (virtual bus) */
    ret = i2c_add_adapter(&stub_adapter);
    if (ret) {
        pr_err("i2c-stub: failed to add adapter\n");
        return ret;
    }

    /* Register the dummy driver */
    ret = i2c_add_driver(&stub_driver);
    if (ret) {
        pr_err("i2c-stub: failed to add driver\n");
        i2c_del_adapter(&stub_adapter);
        return ret;
    }

    /* Create a client device for each address */
    for (i = 0; i < num_chips; i++) {
        struct i2c_board_info info = {
            I2C_BOARD_INFO("stub", chip_addr[i]),
        };
        stub_clients[i] = i2c_new_client_device(&stub_adapter, &info);
        if (IS_ERR(stub_clients[i])) {
            pr_err("i2c-stub: failed to create client at 0x%02x\n",
                   chip_addr[i]);
            stub_clients[i] = NULL;
        } else {
            pr_info("i2c-stub: registered device at 0x%02x\n",
                    chip_addr[i]);
        }
    }

    pr_info("i2c-stub: loaded with %d virtual device(s)\n", num_chips);
    return 0;
}

static void __exit i2c_stub_exit(void)
{
    int i;

    /* Unregister all client devices */
    for (i = 0; i < num_chips; i++) {
        if (stub_clients[i])
            i2c_unregister_device(stub_clients[i]);
    }

    i2c_del_driver(&stub_driver);
    i2c_del_adapter(&stub_adapter);
    pr_info("i2c-stub: unloaded\n");
}

module_init(i2c_stub_init);
module_exit(i2c_stub_exit);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("I2C stub driver for testing");
MODULE_LICENSE("GPL");
```

Create the `Makefile`:
```bash
nano Makefile
```
Paste this:
```Makefile
obj-m += i2c-stub.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

Compile ans Load:
```bash
make
sudo insmod i2c-stub.ko chip_addr=0x50,0x29
sudo modprobe i2c-dev
````
`sudo modprobe i2c-dev` loads a kernel module from the **offficial kernel module directory** `/lib/modules/$(uname -r)/`. 
Unlike `insmod` needs an exact file path.

**Continue to Step 1.**

## Step 1: Load i2c-stub Module

The `i2c-stub` module creates a fake I2C bus with virtual devices at addresses you specify.

Load it with two fake devices (addresses `0x50` and `0x29`):
```bash
sudo modprobe i2c-stub chip_addr=0x50,0x29
```

**Why these addresses?**
- `0x50` — Common EEPROM address
- `0x29` — VL53L1X distance sensor address

Verify it loaded:
```bash
lsmod | grep i2c_stub
```

You should see:
```
i2c_stub    16384    0
```

## Step 2: Discover I2C Buses

List all I2C buses in the system:
```bash
i2cdetect -l
```

You should see something like:
```
i2c-0    i2c    SMBus stub driver          I2C adapter
i2c-1    i2c    bcm2835 (i2c@7e804000)    I2C adapter
i2c-10   i2c    bcm2835 (i2c@7e205000)    I2C adapter
```

**`i2c-0`** (or i2c-11 in my case) is the stub bus (virtual). The others are real hardware buses on the Raspberry Pi.

## Step 3: Scan for Devices
Scan the stub bus to see your virtual devices (`11` for my case):
```bash
i2cdetect -y 0
```

You should see a grid like this:
```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:         -- -- -- -- -- -- -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- UU -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: UU -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
```

**`UU`** at `0x29` and `0x50` means devices are present!

**What the symbols mean:**
- `--` = No device
- `UU` = Device present and in use
- `XX` = Device address (if not in use)

## Step 4: Read and Write Registers
`i2c-stub` simulates a device with 256 registers (addresses `0x00`–`0xFF`).
### Write to a Register

Write value `0xAB` to register `0x00` of device at address `0x50`:
```bash
i2cset -y 0 0x50 0x00 0xAB
```
#### Expection
If there eixsts an ERROR:
```
Error: Could not set address to 0x50: Device or resource busy
```
The `dummy` driver is automatically binding because Linux sees the **dummy** device type and binds its built-in dummy driver to it.
Unbind them:
```bash
# Unbind both
echo "11-0050" | sudo tee /sys/bus/i2c/devices/11-0050/driver/unbind
echo "11-0029" | sudo tee /sys/bus/i2c/devices/11-0029/driver/unbind

# Verify — should say "No such file" meaning no driver bound
ls -la /sys/bus/i2c/devices/11-0050/driver
```
#### Prevent Auto-Binding Permanently
To stop the dummy driver from auto-binding on every `insmod`, blacklist it:
```
echo "blacklist dummy" | sudo tee /etc/modprobe.d/blacklist-dummy.conf
```
```
nano ~/load-i2c-stub.sh
```
```bash
#!/bin/bash
# Load i2c-stub and release addresses for userspace access

sudo rmmod i2c_stub 2>/dev/null
sudo insmod ~/kernel-modules/i2c-stub/i2c-stub.ko chip_addr=0x50,0x29

# Get bus number dynamically
BUS=$(i2cdetect -l | grep stub | awk '{print $1}' | sed 's/i2c-//')
echo "Stub loaded on bus $BUS"

# Unbind dummy driver from all stub devices
for dev in /sys/bus/i2c/devices/${BUS}-*/driver; do
    DEVNAME=$(echo $dev | grep -o "${BUS}-[0-9a-f]*")
    echo "Unbinding $DEVNAME..."
    echo "$DEVNAME" | sudo tee /sys/bus/i2c/devices/$DEVNAME/driver/unbind
done

echo "Done! Testing bus $BUS..."
i2cdetect -y $BUS
```
```
chmod +x ~/load-i2c-stub.sh
~/load-i2c-stub.sh
```
### Read from a Register

Read it back:
```bash
i2cget -y 0 0x50 0x00
```

Output:
```
0xab
```

**Success!** This is how to communicate with real I2C sensors.

### Dump All Registers

See all register contents at once:
```bash
i2cdump -y 0 0x50
```

You'll see a 16×16 grid showing all 256 register values.

---

## Step 5: Explore sysfs Hierarchy

The Linux kernel exposes I2C devices in `/sys/bus/i2c`. Let's explore:
```bash
ls /sys/bus/i2c/devices/
```

You should see:
```
0-0029    0-0050    i2c-0    i2c-1    i2c-10
```

- **`0-0029`** = Bus 0, device at address `0x29`
- **`0-0050`** = Bus 0, device at address `0x50`

Inspect a device:
```bash
ls /sys/bus/i2c/devices/0-0050/
```

You'll see files like:
- `name` — Device name
- `uevent` — Device information
- `driver` — Link to driver (if bound)

Read the device name:
```bash
cat /sys/bus/i2c/devices/0-0050/name
```

Output:
```
stub chip
```
or:
```
dummy
```

---

## Step 6: Understanding I2C Addressing

I2C addresses are 7-bit, but often in different formats:

| Format        | Example | Usage                        |
|---------------|---------|------------------------------|
| 7-bit         | `0x3C`  | Linux kernel, `i2c-tools`    |
| 8-bit write   | `0x78`  | Some datasheets              |
| 8-bit read    | `0x79`  | Some datasheets              |

**Conversion:** `8-bit write = (7-bit << 1) | 0`

Example: `0x3C << 1 = 0x78`

---

## Step 7: Practice Exercises

**Exercise 1:** Write `0xFF` to register `0x10` of device `0x29`, then read it back.
```bash
i2cset -y 0 0x29 0x10 0xFF
i2cget -y 0 0x29 0x10
```

**Exercise 2:** Write values to 5 consecutive registers, then dump all registers.
```bash
i2cset -y 0 0x50 0x00 0x11
i2cset -y 0 0x50 0x01 0x22
i2cset -y 0 0x50 0x02 0x33
i2cset -y 0 0x50 0x03 0x44
i2cset -y 0 0x50 0x04 0x55
i2cdump -y 0 0x50
```

**Exercise 3:** Unload and reload `i2c-stub` with different addresses.
```bash
sudo rmmod i2c_stub
sudo modprobe i2c-stub.ko chip_addr=0x3C,0x60
i2cdetect -y 0
```

---

## What To Learn

- **I2C subsystem layers:** Core, Adapter, Client
- **`i2c-stub`:** Virtual I2C devices for testing without hardware
- **`i2c-tools`:** `i2cdetect`, `i2cget`, `i2cset`, `i2cdump`
- **sysfs hierarchy:** `/sys/bus/i2c/devices/`
- **I2C addressing:** 7-bit vs 8-bit formats

```
The i2c-stub.ko
    → registers a virtual i2c adapter (bus 11) with fake devices
    → kernel knows bus 11 exists
    → but userspace can't touch it yet

modprobe i2c-dev
    → sees ALL registered buses (including bus 11)
    → creates /dev/i2c-11 device file
    → now userspace tools can open /dev/i2c-11

i2cset / i2cget / i2cdetect
    → open /dev/i2c-11
    → send ioctl() calls into kernel
    → kernel routes to your stub_xfer() function
    → your registers[][] array gets read/written
```
