# 2: SPI LED Matrix
## Master SPI Communication with Visual Feedback

## Objective
Learn SPI protocol by controlling an 8x8 LED matrix. When motion is detected, display an animated pattern on the matrix. This teaches SPI's 4-wire communication, chip select logic, and multi-byte data transfers.

## Wokwi Setup
### Add Components
1. Keep your existing circuit from Exercise 1
2. Click the blue **'+'** button
3. Search for **'MAX7219'** and add the 8x8 LED Matrix module

### Wiring Table

| MAX7219 Pin | Function     | Pico Pin  | SPI Signal            |
|-------------|--------------|-----------|----------------------|
| VCC         | Power        | Pico 5V   | 5V supply            |
| GND         | Ground       | Pico GND  | Common ground        |
| DIN         | Data In      | GP3       | MOSI (Master Out)    |
| CLK         | Clock        | GP2       | SCK (Clock)          |
| CS          | Chip Select  | GP5       | CS (Chip Select)     |

> **Note:** MAX7219 doesn't use MISO because it only receives data, doesn't send back. This is called **'simplex'** vs **'full-duplex'** SPI.

## SPI Concepts 

**1. Chip Select (CS) — Active Low**

CS pin controls which device listens. Pull LOW to activate, HIGH to deactivate.
```python
cs.value(0)    # Activate (device listens)
spi.write(...) # Send data
cs.value(1)    # Deactivate (device ignores)
```

**2. Register-Based Communication**

MAX7219 has internal registers. You send `[register_address, data]` pairs.
```python
# Example: Set brightness
REGISTER = 0x0A  # Intensity register
VALUE    = 0x08  # Medium brightness
spi.write(bytes([REGISTER, VALUE]))
```

**3. Clock Speed**

SPI is much faster than I2C. MAX7219 supports up to **10MHz!**

## Challenge Breakdown

**Challenge 1 - SPI Initialization:**
`SPI(0, 10000000, sck=Pin(2), mosi=Pin(3))` — Bus 0, 10MHz speed, clock on GP2, data on GP3. No MISO needed since MAX7219 is receive-only.

**Challenge 2 - CS Starting State:**
`cs.value(1)` — Start HIGH so the chip is deselected and ignores the bus until you're ready.

**Challenges 3 & 5 - CS Control:**
`cs.value(0)` to activate before writing, `cs.value(1)` to deactivate after. The chip ignores all data outside this window.

**Challenge 4 - Sending Data:**
`spi.write(bytes([register, value]))` — MAX7219 expects exactly 2 bytes per command: the register address followed by the data value.

**Challenge 6 - Clear Rows:**
`write_register(row, 0x00)` — Rows are registers `0x01` through `0x08`. Writing `0x00` turns off all 8 LEDs in that row.

**Challenge 7 - Display Pattern:**
`write_register(row + 1, pattern[row])` — The `+ 1` offset maps the 0-indexed list to MAX7219's 1-indexed row registers.

## What to Learn

- **SPI 4-wire protocol:** MOSI, MISO, SCK, CS and when MISO can be omitted (simplex)
- **Active-low chip select:** Critical for multi-device SPI buses
- **Register addressing:** How devices organize internal memory
- **Binary patterns:** Direct hardware control using binary literals (`0b00111100`)
- **Clock speed selection:** Speed vs compatibility tradeoffs

<img width="757" height="910" alt="image" src="https://github.com/user-attachments/assets/c5cee59a-0877-4616-a2ae-77fbf444f085" />
