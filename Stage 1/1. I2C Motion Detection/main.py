from machine import Pin, I2C 
from ssd1306 import SSD1306_I2C

import time  # Setup I2C for OLED display 

# CHALLENGE 1: Initialize I2C with correct pins 
# Hint: I2C(bus_number, sda=Pin(?), scl=Pin(?)) 
i2c = I2C(0, sda=Pin(0), scl=Pin(1), freq=400000)  

# Initialize display (128x64 pixels) 
display = SSD1306_I2C(128, 64, i2c)  

# Setup PIR motion sensor 
# CHALLENGE 2: Create Pin object for motion sensor 
# Hint: Pin(pin_number, Pin.IN) 
motion_sensor = Pin(15, Pin.IN)  

# Setup LED 
# CHALLENGE 3: Create Pin object for LED output 
led = Pin(14, Pin.OUT)

def show_message(text):     
    display.fill(0)  # Clear display     
    display.text(text, 0, 0)     
    display.show()  
    
# Startup
show_message('System Ready')
time.sleep(2)

print('Motion detector started')

last_state = 0

while True:
    # CHALLENGE 4: Read motion sensor value     
    # Hint: sensor.value() returns 0 or 1  
    current_state = motion_sensor.value()
    
    # CHALLENGE 5: Edge Dectection
    # Track the previous state (last_state)
    # Only print when we see a transition from 0 to 1 (rising edge)
    if current_state == 1 and last_state == 0: 
        led.on()
        show_message('MOTION!')
        print('Motion detected!')
        time.sleep(2)
        led.off()
        show_message('Monitoring...')
    
    last_state = current_state
    time.sleep(0.1)
# while True:           
    # CHALLENGE 4: Read motion sensor value     
    # Hint: sensor.value() returns 0 or 1     
    motion_detected = motion_sensor.value()          
    if motion_detected:         
        # CHALLENGE 5: Turn LED on and show alert         
        led.on()        
        show_message('MOTION!')         
        print('Motion detected!')         
        time.sleep(2)    
    else:         
        # CHALLENGE 6: Turn LED off and show monitoring         
        led.off()        
        show_message('Monitoring...')
        # print('Monitoring...')           
        time.sleep(0.1)
