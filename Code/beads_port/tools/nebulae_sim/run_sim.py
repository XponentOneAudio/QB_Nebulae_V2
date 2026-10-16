# Runs the real Nebulae program (Code/nebulae/nebulae.py) on a desktop with
# fake hardware (fakehw/) and Csound's silent "null" audio driver instead of
# the WM8731. Python 2.7, like the module. Used by sim.sh.
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NEBULAE = "/home/alarm/QB_Nebulae_V2/Code/nebulae"
sys.path.insert(0, NEBULAE)
sys.path.insert(0, os.path.join(HERE, "fakehw"))

import ctcsound

_set_option = ctcsound.Csound.setOption
REPLACE = {
    "-iadc:hw:0,0": "-iadc",
    "-odac:hw:0,0": "-odac",
    "-+rtaudio=alsa": "-+rtaudio=null",
}


def set_option(self, option):
    return _set_option(self, REPLACE.get(option, option))


ctcsound.Csound.setOption = set_option

import threading
import time

sys.argv = [os.path.join(NEBULAE, "nebulae.py")]
GLOBALS = {"__name__": "__main__", "__file__": sys.argv[0]}

# NEBSIM_CRASH_INSTR=name: make Nebulae crash while starting that instrument
# (in ControlHandler, where the settings bug crashed it), to test recovery.
CRASH_INSTR = os.environ.get("NEBSIM_CRASH_INSTR")
if CRASH_INSTR:
    import controlhandler
    _init = controlhandler.ControlHandler.__init__

    def crashing_init(self, csound, numberFiles, configData, instr='a_granularlooper', bank='factory'):
        if instr == CRASH_INSTR:
            raise RuntimeError("NEBSIM: injected crash while starting " + instr)
        _init(self, csound, numberFiles, configData, instr, bank)

    controlhandler.ControlHandler.__init__ = crashing_init


# NEBSIM_SWITCH_TO=bank/instr, NEBSIM_SWITCH_AFTER=seconds: select another
# instrument at run time, as the instrument selector does (select a slot,
# then click the Speed encoder).
def switcher():
    target = os.environ.get("NEBSIM_SWITCH_TO")
    if not target:
        return
    bank, instr = target.split("/")
    time.sleep(float(os.environ.get("NEBSIM_SWITCH_AFTER", "8")))
    app = GLOBALS.get("app")
    while app is None or app.ui is None or app.c_handle is None:
        time.sleep(0.5)
        app = GLOBALS.get("app")
    handles = {"factory": app.ui.factoryinstr_fhandle,
               "user": app.ui.userinstr_fhandle,
               "puredata": app.ui.puredata_fhandle}
    index = handles[bank].getIndex(instr)
    print("NEBSIM: switching to %s/%s (slot %s)" % (bank, instr, index))
    sys.stdout.flush()
    app.c_handle.setInstrSelBank(bank)
    app.c_handle.setInstrSelIdx(index)
    app.ui.reload_flag = True


thread = threading.Thread(target=switcher)
thread.daemon = True
thread.start()

os.chdir("/")
print("NEBSIM: starting nebulae.py")
sys.stdout.flush()
execfile(sys.argv[0], GLOBALS)
