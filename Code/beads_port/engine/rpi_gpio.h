// Copyright 2026 The Pearls (Beads for Nebulae) contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// -----------------------------------------------------------------------------
//
// Direct access to the Nebulae gate jacks through /dev/gpiomem (BCM2835/7
// GPIO registers), bypassing the Python control loop for timing.
//
// The Python layer has already configured the pins (inputs with pull-ups for
// the gate inputs, output for EOL); this class only reads levels and sets or
// clears the EOL output. All Nebulae gate jacks are active low at the pin.

#ifndef PEARLS_ENGINE_RPI_GPIO_H_
#define PEARLS_ENGINE_RPI_GPIO_H_

#include <cstdint>

namespace pearls {

class NebulaeGates {
 public:
  // BCM pin numbers (nebulae/controlhandler.py).
  enum {
    PIN_FREEZE = 4,
    PIN_RESET = 25,
    PIN_NEXT = 23,
    PIN_RECORD = 24,
    PIN_EOL_OUT = 16,
  };

  NebulaeGates() : gpio_(0) { }
  ~NebulaeGates();

  // Returns false if /dev/gpiomem is not available (not a Raspberry Pi, or
  // no permission); all reads then return false and writes do nothing.
  bool Init();

  bool available() const { return gpio_ != 0; }

  // True when the gate at the jack is high.
  bool gate(int pin) const {
    if (!gpio_) {
      return false;
    }
    return !(gpio_[kGplev0] & (1u << pin));
  }

  void set_eol(bool high) {
    if (!gpio_) {
      return;
    }
    // Active low: a high gate pulls the pin low.
    if (high) {
      gpio_[kGpclr0] = 1u << PIN_EOL_OUT;
    } else {
      gpio_[kGpset0] = 1u << PIN_EOL_OUT;
    }
  }

 private:
  static const int kGpset0 = 0x1c / 4;
  static const int kGpclr0 = 0x28 / 4;
  static const int kGplev0 = 0x34 / 4;

  volatile uint32_t* gpio_;
};

}  // namespace pearls

#endif  // PEARLS_ENGINE_RPI_GPIO_H_
