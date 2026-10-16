# Fake RPi.GPIO for running the Nebulae Python code on a desktop (Python 2).
# Inputs read as "idle": gate jacks and encoder pins high (active low), and
# the CD4021 shift register (data pin 5, latch 15, clock 14) returns 0 for
# the buttons and 1 for the Source gate bit (idle after the hardware flip).
# Buttons can be "pressed" by writing bit numbers to $NEBSIM_BUTTONS_FILE
# (shiftregister.py: 0 reset, 3 record, 4 freeze, 5 source, 7 next).
# Replace the file atomically (write another file, then mv): "echo > file"
# empties it for a moment, which releases every button, e.g. Source while
# it is held for the secondary layer.
import os

BCM = 11
BOARD = 10
IN = 1
OUT = 0
HIGH = 1
LOW = 0
PUD_UP = 22
PUD_DOWN = 21
PUD_OFF = 20
BOTH = 33
RISING = 31
FALLING = 32

_SR_DATA, _SR_LATCH = 5, 15
_SOURCE_GATE_BIT = 6
_sr_bit = [0]
_outputs = {}


def setmode(mode): pass
def setwarnings(flag): pass
def setup(pin, direction, pull_up_down=None, initial=None): pass
def cleanup(*args): pass
def add_event_detect(pin, edge, callback=None, bouncetime=None): pass
def remove_event_detect(pin): pass


def _pressed_buttons():
    path = os.environ.get("NEBSIM_BUTTONS_FILE")
    if not path or not os.path.exists(path):
        return set()
    with open(path) as f:
        return set(int(x) for x in f.read().split())


def output(pin, value):
    _outputs[pin] = value
    if pin == _SR_LATCH and value:
        _sr_bit[0] = 0


def input(pin):
    if pin == _SR_DATA:
        bit = _sr_bit[0]
        _sr_bit[0] += 1
        if bit == _SOURCE_GATE_BIT:
            return 1
        return 1 if bit in _pressed_buttons() else 0
    return 1


class PWM(object):
    def __init__(self, pin, freq): pass
    def start(self, duty): pass
    def stop(self): pass
    def ChangeDutyCycle(self, duty): pass
    def ChangeFrequency(self, freq): pass
