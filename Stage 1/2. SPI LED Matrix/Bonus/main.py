from machine import Pin, SPI, I2C
from ssd1306 import SSD1306_I2C
import time  

# MAX7219 Register addresses 
REG_NOOP = 0x00 
REG_DECODE_MODE = 0x09 
REG_INTENSITY = 0x0A 
REG_SCAN_LIMIT = 0x0B 
REG_SHUTDOWN = 0x0C 
REG_DISPLAY_TEST = 0x0F  

# Initialize I2C for OLED (from 1)
i2c = I2C(0, sda=Pin(0), scl=Pin(1), freq=400000)
display = SSD1306_I2C(128,64, i2c)

# Setup motion sensor and LED (from 1)
motion_sensor = Pin(15, Pin.IN)
led = Pin(14, Pin.OUT)

# # Bus 0, 10MHz, clock on GP2, data out on GP3
spi = SPI(0, baudrate=10000000, sck=Pin(2), mosi=Pin(3))  

# Setup CS pin - starts HIGH (inactive) 
cs = Pin(5, Pin.OUT) 
cs.value(1)  # Start deselected

def write_register(register, value):     
    """Send command to MAX7219"""            
    cs.value(0)     # Activate chip select

    # Send register and value "bytes([register, value])""       
    spi.write(bytes([register, value])) 
   
    cs.value(1)     # Deactivate chip select

def init_display():     
    """Initialize MAX7219"""     
    write_register(REG_SHUTDOWN, 0x00)      # Shutdown mode
    write_register(REG_DISPLAY_TEST, 0x00)  # Normal operation
    write_register(REG_DECODE_MODE, 0x00)   # No decode (raw LED control)
    write_register(REG_SCAN_LIMIT, 0x07)    # Scan all 8 rows
    write_register(REG_INTENSITY, 0x08)     # Medium brightness (0x00-0x0F)
    write_register(REG_SHUTDOWN, 0x01)      # Normal operation         
    clear_display()  

def clear_display():     
    """Turn off all LEDs"""     
    # Clear all 8 rows, registers 0x01 to 0x08     
    for row in range(1, 9):         
        write_register(row, 0x00)  
        
def show_pattern(pattern):     
    """Display 8x8 pattern     
    pattern: list of 8 bytes, each byte = one row
    Bit 1 = LED on, Bit 0 = LED off     
    """     
    # Write pattern to rows 1-8     
    for row in range(8):         
        write_register(row + 1, pattern[row])  

def show_message_oled(text):
    """Update OLED display"""
    display.fill(0)
    display.text(text, 0, 0)
    display.show()

# LED Test patterns 
smiley = [
    0b00111100,  # Row 1:   ****
    0b01000010,  # Row 2:  *    *
    0b10100101,  # Row 3: * ** * *
    0b10000001,  # Row 4: *      *
    0b10100101,  # Row 5: * ** * *
    0b10011001,  # Row 6: *  **  *
    0b01000010,  # Row 7:  *    *
    0b00111100   # Row 8:   ****
]

alert = [
    0b11111111,  # Full border
    0b11111111,
    0b11000011,
    0b11000011,
    0b11000011,
    0b11000011,
    0b11111111,
    0b11111111
]

exclamation = [
    0b00011000,  #    **
    0b00011000,  #    **
    0b00011000,  #    **
    0b00011000,  #    **
    0b00011000,  #    **
    0b00000000,  #
    0b00011000,  #    **
    0b00011000   #    **
]

# Initialize everything
print('Initializing...') 
init_display()
show_message_oled('System Ready')
print('MAX7219 initialized') 
time.sleep(2)

# Show startup animation
show_pattern(smiley)
time.sleep(2)
clear_display()

print('Motion detector started')
show_message_oled('Monitoring...')

# Main loop with motion detection
last_state = 0

while True:
    current_state = motion_sensor.value();

    if current_state == 1 and last_state == 0:
        print('MOTION DETECTED!')

        # Turn on LED
        led.on()

        # Show alert on OLED
        show_message_oled('MOTION!')
        
        # Show alert pattern on LED matrix
        show_pattern(alert)
        time.sleep(0.5)

        # Flash exclamation
        show_pattern(exclamation)
        time.sleep(0.5)
        show_pattern(alert)
        time.sleep(0.5)
        show_pattern(exclamation)
        time.sleep(0.5)

        # Show smiley
        show_pattern(smiley)
        time.sleep(2)

        # Turn off LED and clear displays
        led.off()
        clear_display()
        show_message_oled('Monitoring...')

    last_state = current_state
    time.sleep(0.1)
