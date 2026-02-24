from machine import Pin, SPI 
import time  

# MAX7219 Register addresses 
REG_NOOP = 0x00 
REG_DECODE_MODE = 0x09 
REG_INTENSITY = 0x0A 
REG_SCAN_LIMIT = 0x0B 
REG_SHUTDOWN = 0x0C 
REG_DISPLAY_TEST = 0x0F  

# CHALLENGE 1: Initialize SPI 
# Hint: SPI(bus, baudrate, sck=Pin(?), mosi=Pin(?))
# MAX7219 supports up to 10MHz (10000000) 
spi = SPI(0, baudrate=10000000, sck=Pin(2), mosi=Pin(3))  

# CHALLENGE 2: Setup CS pin 
# Hint: Should be OUTPUT, starts HIGH (inactive) 
cs = Pin(5, Pin.OUT) 
cs.value(1)  # Start deselected

def write_register(register, value):     
    """Send command to MAX7219"""     
    # CHALLENGE 3: Activate chip select     
    cs.value(0)     

    # CHALLENGE 4: Send register and value     
    # Hint: bytes([register, value])     
    spi.write(bytes([register, value])) 

    # CHALLENGE 5: Deactivate chip select     
    cs.value(1)  

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
    # CHALLENGE 6: Clear all 8 rows     
    # Hint: Rows are registers 0x01 to 0x08     
    for row in range(1, 9):         
        write_register(row, 0x00)  
        
def show_pattern(pattern):     
    """Display 8x8 pattern     
    pattern: list of 8 bytes, each byte = one row
    Bit 1 = LED on, Bit 0 = LED off     
    """     
    # CHALLENGE 7: Write pattern to rows 1-8     
    for row in range(8):         
        write_register(row + 1, pattern[row])  

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

# Initialize init_display() 
print('MAX7219 initialized')  
init_display()
time.sleep(2)

# Show smiley face 
print('Showing smiley ...')
show_pattern(smiley) 
time.sleep(2)  

# Show alert pattern 
print('Showing alert ...')
show_pattern(alert) 
time.sleep(2)  

clear_display() 
print('Test complete!')
