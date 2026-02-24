# 1: I2C Motion Detection
## Objective
Build a motion sensor that triggers an LED and displays a message on an OLED screen.

## Wokwi Setup

1. Click **'MICROPYTHON'** template
2. Click the blue **'+'** button, add these components:
   - SSD1306 OLED Display (I2C)
   - PIR Motion Sensor
   - LED (red)
   - Resistor (220Ω for LED)

3. Wire the components:

| Component      | Pin         | Connects To | Notes           |
|----------------|-------------|-------------|-----------------|
| OLED Display   | VCC         | Pico 3.3V   | Power           |
| OLED Display   | GND         | Pico GND    | Ground          |
| OLED Display   | SDA         | GP0         | I2C Data        |
| OLED Display   | SCL         | GP1         | I2C Clock       |
| PIR Sensor     | VCC         | Pico 3.3V   | Power           |
| PIR Sensor     | GND         | Pico GND    | Ground          |
| PIR Sensor     | OUT         | GP15        | Digital signal  |
| LED (red)      | Anode (+)   | GP14        | Via 220Ω resistor |
| LED (red)      | Cathode (-) | Pico GND    | Ground          |

4. Add a new library file `ssd1306.py` for the module.

## Hints for Challenges

**Challenge 1 - I2C Initialization:**
Look at your wiring table. Which GPIO pins did you connect SDA and SCL to? Remember GP0 is `Pin(0)` in MicroPython.

**Challenge 2 - Motion Sensor Pin:**
The PIR sensor outputs a digital signal (HIGH when motion, LOW when none). Use `Pin.IN` mode.

**Challenge 3 - LED Pin:**
The LED needs to be controlled as an output. Use `Pin.OUT` mode.

**Challenge 4 - Reading Sensor:**
Call the `.value()` method on your sensor Pin object. It returns `0` (no motion) or `1` (motion detected).

**Challenges 5 & 6 - LED Control:**
Use `.value(1)` to turn on and `.value(0)` to turn off. Or use the shortcuts `.on()` and `.off()`.

## What To Learn

- **I2C addressing:** The OLED has a fixed address (`0x3C`). Multiple I2C devices share SDA/SCL.
- **Digital I/O basics:** Reading input (sensor) vs writing output (LED).
- **Event-driven logic:** The system only responds when motion is detected.

<img width="755" height="896" alt="image" src="https://github.com/user-attachments/assets/0def7832-5cc6-4e6d-bef5-14c56e0373c5" />
