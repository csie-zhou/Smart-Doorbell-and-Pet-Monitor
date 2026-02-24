# Bonus: Integrate with Motion Sensor
Once your LED matrix works, combine it with `1. I2C Motion Detection`:
```python
# Keep motion sensor from Exercise 1
motion_sensor = Pin(15, Pin.IN)
last_state = 0

while True:
    current_state = motion_sensor.value()

    if current_state == 1 and last_state == 0:  # Motion detected!
        show_pattern(alert)   # Show alert on matrix
        time.sleep(2)
        show_pattern(smiley)  # Show smiley

    last_state = current_state
    time.sleep(0.1)
```
