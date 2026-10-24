# Fake Adafruit_GPIO.SPI: every MCP3208 channel reads mid-scale (2048):
# pots at 12 o'clock, CV inputs at 0 V.
MSBFIRST = 0
LSBFIRST = 1


class SpiDev(object):
    def __init__(self, port, device, max_speed_hz=500000):
        pass
    def set_clock_hz(self, hz): pass
    def set_mode(self, mode): pass
    def set_bit_order(self, order): pass
    def close(self): pass
    def transfer(self, data):
        return [0, 0x08, 0x00]


class BitBang(SpiDev):
    def __init__(self, *args, **kwargs):
        pass
