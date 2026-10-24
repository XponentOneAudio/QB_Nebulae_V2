#!/bin/bash
# Builds the Beads CPU benchmark against a checkout of pichenettes/eurorack
# (with the stmlib submodule initialised).
#
# Usage:
#   ./build_bench.sh /path/to/eurorack            # native build (desktop or on the Nebulae)
#   CXX=arm-linux-gnueabihf-g++ EXTRA="-static" ./build_bench.sh /path/to/eurorack
#
# Run: ./beads_bench          -> % of one core needed for real time, per mode
#      ./beads_bench check    -> mean |output| per mode (sanity check across targets)

set -e
EURORACK=${1:?path to eurorack checkout}
HERE=$(cd "$(dirname "$0")" && pwd)
CXX=${CXX:-g++}

case "$($CXX -dumpmachine)" in
  arm*) ARCH_FLAGS="-mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard" ;;
  *) ARCH_FLAGS="" ;;
esac

cd "$EURORACK"
$CXX -O2 $ARCH_FLAGS $EXTRA -DTEST -include cstdio -Wno-unused-local-typedefs -I. \
  -o "$HERE/beads_bench" \
  "$HERE/bench.cc" \
  beads/dsp/processor.cc beads/dsp/synth.cc beads/dsp/mu_law.cc \
  beads/dsp/delay_clock.cc beads/dsp/delay_controller.cc \
  beads/dsp/trigger_generator.cc beads/resources.cc \
  stmlib/utils/random.cc stmlib/dsp/units.cc
echo "built $HERE/beads_bench"
