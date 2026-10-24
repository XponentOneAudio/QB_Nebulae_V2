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
// CPU benchmark of the complete Pearls engine (gain control, resampling,
// Beads processor) under worst-case settings. Run it on the Nebulae with the
// Nebulae service stopped: the result is the share of one CPU core needed
// to run in real time.

#include <time.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engine/pearls_engine.h"

using namespace pearls;

namespace {

double Now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
}

double Run(int quality, bool mono, bool delay, bool synth, double seconds) {
  Engine engine;
  engine.Init();
  Controls c;
  c.SetDefaults();
  c.quality = quality;
  c.mono = mono;
  c.synth = synth;
  c.size = delay ? 1.0f : 0.6f;
  c.density = 0.95f;  // near-maximum grain rate
  c.time_random = c.size_random = c.shape_random = c.pitch_random = 0.3f;
  c.pitch = 7.0f;
  c.feedback = 0.5f;
  c.dry_wet = 0.8f;
  c.reverb = 0.6f;

  const size_t kBlock = 64;
  float in_l[kBlock], in_r[kBlock], out_l[kBlock], out_r[kBlock];
  uint8_t gate[kBlock];
  const size_t total = size_t(seconds * Engine::kSampleRate);
  double phase = 0.0;
  double elapsed = 0.0;
  for (size_t n = 0; n < total; n += kBlock) {
    for (size_t i = 0; i < kBlock; ++i) {
      phase += 220.0 / Engine::kSampleRate;
      if (phase >= 1.0) phase -= 1.0;
      in_l[i] = 0.5f * sinf(2.0f * float(M_PI) * float(phase)) +
          0.02f * (rand() / float(RAND_MAX) - 0.5f);
      in_r[i] = -in_l[i];
    }
    const double start = Now();
    engine.Process(c, in_l, in_r, NULL, NULL, out_l, out_r, gate, kBlock);
    elapsed += Now() - start;
  }
  return elapsed / seconds;
}

}  // namespace

int main(int argc, char** argv) {
  const double seconds = argc > 1 ? atof(argv[1]) : 10.0;
  const char* names[] = { "dirty tape", "clean tape", "vintage digital",
                          "digital" };
  double worst = 0.0;
  printf("%% of one CPU core needed for real time (%.0f s per test)\n",
         seconds);
  for (int quality = 0; quality < 4; ++quality) {
    for (int mono = 0; mono < 2; ++mono) {
      const double g = Run(quality, mono, false, false, seconds);
      const double d = Run(quality, mono, true, false, seconds);
      worst = std::max(worst, std::max(g, d));
      printf("  %-16s %-6s granular %5.1f%%   delay %5.1f%%\n",
             names[quality], mono ? "mono" : "stereo", g * 100.0, d * 100.0);
    }
  }
  const double s = Run(QUALITY_DIGITAL, false, false, true, seconds);
  worst = std::max(worst, s);
  printf("  %-16s        %5.1f%%\n", "synth", s * 100.0);
  printf("worst case: %.1f%%\n", worst * 100.0);
  return 0;
}
