# Pearls: Mutable Instruments Beads DSP ported to the Qu-Bit Nebulae v2.
#
#   make              Csound opcode library  build/libpearls.so
#   make test         build and run the engine tests
#   make render       desktop renderer       build/pearls_render
#   make bench        CPU benchmark          build/pearls_bench
#   make prebuilt     portable Nebulae binary prebuilt/armv7/libpearls.so
#                     (see tools/build_prebuilt.sh)
#
# Cross-compiling: make CXX=arm-linux-gnueabihf-g++ CSOUND_INCLUDE=...
# Single precision Csound: make CSOUND_DOUBLE=0
# Extra compiler flags: make CXXFLAGS="-O3 ..." (added to the required ones)

CXX ?= g++
BUILD ?= build
CSOUND_INCLUDE ?= $(firstword $(wildcard /usr/local/include/csound /usr/include/csound))
CSOUND_DOUBLE ?= 1

TARGET_MACHINE := $(shell $(CXX) -dumpmachine)
ifneq (,$(filter arm%,$(TARGET_MACHINE)))
  # Raspberry Pi 3 / Compute Module 3 running a 32-bit (armv7h) system.
  ARCH_FLAGS ?= -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
endif

CXXFLAGS ?= -O2 -g
ALL_CXXFLAGS = $(CXXFLAGS) $(ARCH_FLAGS) -fPIC -Wall -Wno-unused-local-typedefs \
	-DTEST -DBEADS_NO_HARDWARE_24K -include cstdio \
	-I. -Ithird_party/eurorack

UPSTREAM_SRC = \
	third_party/eurorack/beads/dsp/processor.cc \
	third_party/eurorack/beads/dsp/synth.cc \
	third_party/eurorack/beads/dsp/mu_law.cc \
	third_party/eurorack/beads/dsp/delay_clock.cc \
	third_party/eurorack/beads/dsp/delay_controller.cc \
	third_party/eurorack/beads/dsp/trigger_generator.cc \
	third_party/eurorack/beads/resources.cc \
	third_party/eurorack/stmlib/utils/random.cc \
	third_party/eurorack/stmlib/dsp/units.cc

ENGINE_SRC = \
	engine/pearls_engine.cc \
	engine/gain_control.cc \
	engine/rpi_gpio.cc

LIB_OBJ = $(patsubst %.cc,$(BUILD)/obj/%.o,$(UPSTREAM_SRC) $(ENGINE_SRC))

ifeq ($(CSOUND_DOUBLE),1)
  CSOUND_FLAGS = -DUSE_DOUBLE
endif

.PHONY: all test render bench prebuilt clean

all: $(BUILD)/libpearls.so

$(BUILD)/obj/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) -MMD -c $< -o $@

$(BUILD)/obj/csound/pearls_opcode.o: csound/pearls_opcode.cc
	@test -n "$(CSOUND_INCLUDE)" || (echo "Csound headers not found: set CSOUND_INCLUDE"; exit 1)
	@mkdir -p $(dir $@)
	$(CXX) $(ALL_CXXFLAGS) $(CSOUND_FLAGS) -I$(CSOUND_INCLUDE) -MMD -c $< -o $@

$(BUILD)/libpearls.so: $(LIB_OBJ) $(BUILD)/obj/csound/pearls_opcode.o
	$(CXX) -shared $(ARCH_FLAGS) -o $@ $^

$(BUILD)/pearls_test: $(LIB_OBJ) $(BUILD)/obj/tools/pearls_test.o
	$(CXX) $(ARCH_FLAGS) -o $@ $^

$(BUILD)/pearls_render: $(LIB_OBJ) $(BUILD)/obj/tools/pearls_render.o
	$(CXX) $(ARCH_FLAGS) -o $@ $^

$(BUILD)/pearls_bench: $(LIB_OBJ) $(BUILD)/obj/tools/pearls_bench.o
	$(CXX) $(ARCH_FLAGS) -o $@ $^

test: $(BUILD)/pearls_test
	$(BUILD)/pearls_test

render: $(BUILD)/pearls_render

bench: $(BUILD)/pearls_bench

prebuilt:
	tools/build_prebuilt.sh

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
