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
// Engine tests: run on the desktop or on the Nebulae itself (make test).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

#include "engine/nebulae_mapping.h"
#include "engine/pearls_engine.h"
#include "engine/rational_resampler.h"

using namespace pearls;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond, ...) do { \
  ++checks; \
  if (!(cond)) { \
    ++failures; \
    printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    printf(__VA_ARGS__); \
    printf("\n"); \
  } \
} while (0)

const float kSr = float(Engine::kSampleRate);

struct Signals {
  std::vector<float> in_l, in_r, out_l, out_r;
  std::vector<uint8_t> freeze, seed, gate;
};

typedef std::function<void(size_t frame, Controls* c)> ControlScript;
typedef std::function<float(size_t frame, int channel)> InputFn;
typedef std::function<bool(size_t frame)> GateFn;

// Runs the engine for `seconds`, in blocks of odd sizes to exercise the FIFO.
Signals Run(
    Engine* engine,
    float seconds,
    InputFn input,
    ControlScript script,
    GateFn freeze_gate = GateFn(),
    GateFn seed_gate = GateFn()) {
  const size_t total = size_t(seconds * kSr);
  Signals s;
  s.in_l.resize(total);
  s.in_r.resize(total);
  s.out_l.resize(total);
  s.out_r.resize(total);
  s.freeze.resize(total);
  s.seed.resize(total);
  s.gate.resize(total);
  for (size_t i = 0; i < total; ++i) {
    s.in_l[i] = input ? input(i, 0) : 0.0f;
    s.in_r[i] = input ? input(i, 1) : 0.0f;
    s.freeze[i] = freeze_gate ? freeze_gate(i) : 0;
    s.seed[i] = seed_gate ? seed_gate(i) : 0;
  }
  const size_t block_sizes[] = { 64, 37, 128, 48, 5 };
  size_t position = 0;
  size_t b = 0;
  Controls c;
  c.SetDefaults();
  while (position < total) {
    size_t n = std::min(block_sizes[b++ % 5], total - position);
    script(position, &c);
    engine->Process(
        c,
        &s.in_l[position], &s.in_r[position],
        &s.freeze[position], &s.seed[position],
        &s.out_l[position], &s.out_r[position],
        &s.gate[position], n);
    position += n;
  }
  return s;
}

float Rms(const std::vector<float>& x, float from, float to) {
  size_t a = size_t(from * kSr), b = std::min(size_t(to * kSr), x.size());
  double sum = 0.0;
  for (size_t i = a; i < b; ++i) sum += double(x[i]) * x[i];
  return b > a ? float(sqrt(sum / double(b - a))) : 0.0f;
}

bool AllFiniteAndBounded(const Signals& s) {
  for (size_t i = 0; i < s.out_l.size(); ++i) {
    if (!std::isfinite(s.out_l[i]) || !std::isfinite(s.out_r[i]) ||
        fabsf(s.out_l[i]) > 1.0f || fabsf(s.out_r[i]) > 1.0f) {
      return false;
    }
  }
  return true;
}

// Average frequency from positive-going zero crossings.
float ZeroCrossingFrequency(const std::vector<float>& x, float from, float to) {
  size_t a = size_t(from * kSr), b = std::min(size_t(to * kSr), x.size());
  int crossings = 0;
  size_t first = 0, last = 0;
  for (size_t i = a + 1; i < b; ++i) {
    if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
      if (!crossings) first = i;
      last = i;
      ++crossings;
    }
  }
  return crossings > 1 ? (crossings - 1) * kSr / float(last - first) : 0.0f;
}

float Sine(size_t i, float frequency, float amplitude) {
  return amplitude * sinf(2.0f * float(M_PI) * frequency * float(i) / kSr);
}

float Noise() {
  return (rand() / float(RAND_MAX)) * 2.0f - 1.0f;
}

void TestResampler() {
  printf("resampler 48k -> 32k -> 48k\n");
  RationalResampler<2, 3, 192, 12> down;
  RationalResampler<3, 2, 192, 8> up;
  down.Init(14500.0 / 96000.0, 7.0);
  up.Init(14500.0 / 96000.0, 7.0);

  std::vector<float> out;
  std::vector<float> out32;
  for (int pass = 0; pass < 2; ++pass) {
    const float f = pass == 0 ? 1000.0f : 20000.0f;
    down.Reset();
    up.Reset();
    out.clear();
    out32.clear();
    for (size_t block = 0; block < 4000; ++block) {
      float l[12], r[12], l32[8], r32[8], lo[12], ro[12];
      for (size_t i = 0; i < 12; ++i) {
        l[i] = r[i] = Sine(block * 12 + i, f, 0.5f);
      }
      down.Process(l, r, l32, r32, 12);
      out32.insert(out32.end(), l32, l32 + 8);
      up.Process(l32, r32, lo, ro, 8);
      out.insert(out.end(), lo, lo + 12);
    }
    if (pass == 0) {
      const float rms = Rms(out, 0.2f, 0.9f);
      CHECK(fabsf(rms - 0.5f / sqrtf(2.0f)) < 0.01f,
            "1 kHz round trip rms %f (expected %f)", rms, 0.5f / sqrtf(2.0f));
      const float freq = ZeroCrossingFrequency(out, 0.2f, 0.9f);
      CHECK(fabsf(freq - 1000.0f) < 1.0f, "1 kHz round trip frequency %f",
            freq);
    } else {
      // 20 kHz cannot be represented at 32 kHz: it must be filtered out
      // rather than aliased to 12 kHz.
      double sum = 0.0;
      for (size_t i = 8000; i < out32.size(); ++i) sum += out32[i] * out32[i];
      const float rms = sqrtf(float(sum / double(out32.size() - 8000)));
      CHECK(rms < 0.005f, "20 kHz leaks through the 32 kHz path: rms %f", rms);
    }
  }
}

void TestAllModesStable() {
  printf("all qualities, mono/stereo, granular/delay, synth: stable output\n");
  const char* names[] = { "dirty tape", "clean tape", "vintage", "digital" };
  for (int quality = 0; quality < 4; ++quality) {
    for (int mono = 0; mono < 2; ++mono) {
      for (int delay = 0; delay < 2; ++delay) {
        Engine engine;
        engine.Init();
        Signals s = Run(
            &engine, 5.0f,
            [](size_t i, int ch) {
              return Sine(i, ch ? 330.0f : 220.0f, 0.4f) + 0.02f * Noise();
            },
            [=](size_t, Controls* c) {
              c->quality = quality;
              c->mono = mono;
              c->size = delay ? 1.0f : 0.6f;
              c->density = 0.8f;
              // Tape-mode buffers hold up to ~30 s (mono): after 5 s of
              // input, only the most recent part of the buffer has audio.
              c->time = 0.05f;
              c->dry_wet = 1.0f;
              c->feedback = 0.5f;
              c->reverb = 0.5f;
              c->time_random = c->size_random = 0.5f;
              c->shape_random = c->pitch_random = 0.3f;
            });
        const float rms = Rms(s.out_l, 2.5f, 5.0f) + Rms(s.out_r, 2.5f, 5.0f);
        CHECK(AllFiniteAndBounded(s), "%s mono=%d delay=%d: bad samples",
              names[quality], mono, delay);
        CHECK(rms > 0.02f, "%s mono=%d delay=%d: output too quiet (%f)",
              names[quality], mono, delay, rms);
        printf("  %-10s %-6s %-8s wet rms %.3f\n", names[quality],
               mono ? "mono" : "stereo", delay ? "delay" : "granular", rms);
      }
    }
  }
  for (int quality = 0; quality < 4; ++quality) {
    Engine engine;
    engine.Init();
    Signals s = Run(
        &engine, 4.0f, InputFn(),
        [=](size_t, Controls* c) {
          c->synth = true;
          c->quality = quality;
          c->density = 0.8f;
          c->dry_wet = 0.7f;
          c->reverb = 0.3f;
        });
    const float rms = Rms(s.out_l, 1.0f, 4.0f);
    size_t gates = 0;
    for (size_t i = 1; i < s.gate.size(); ++i) {
      gates += s.gate[i] && !s.gate[i - 1];
    }
    CHECK(AllFiniteAndBounded(s), "synth quality %d: bad samples", quality);
    CHECK(rms > 0.01f, "synth quality %d: silent (%f)", quality, rms);
    CHECK(gates > 4, "synth quality %d: only %zu grain gates", quality, gates);
    printf("  synth q%d   rms %.3f, %zu grain gates\n", quality, rms, gates);
  }
}

void TestDryPathAndLatency() {
  printf("dry path: unity-ish gain, %zu frames latency\n", Engine::kLatency);
  Engine engine;
  engine.Init();
  srand(1);
  Signals s = Run(
      &engine, 2.0f,
      [](size_t, int) { return 0.2f * Noise(); },
      [](size_t, Controls* c) {
        c->dry_wet = 0.0f;
        c->reverb = 0.0f;
        c->gain = 1;  // manual gain, ~1x: no AGC
      });
  // Beads' dry gain at dry/wet = 0 is 0.75.
  double err = 0.0, ref = 0.0;
  const size_t from = size_t(0.5f * kSr);
  for (size_t i = from; i < s.out_l.size(); ++i) {
    const float expected = 0.75f * s.in_l[i - Engine::kLatency];
    err += (s.out_l[i] - expected) * (s.out_l[i] - expected);
    ref += expected * expected;
  }
  const float snr = 10.0f * log10f(float(ref / std::max(err, 1e-20)));
  CHECK(snr > 40.0f, "dry path SNR vs delayed input only %.1f dB", snr);
  printf("  dry path SNR %.1f dB\n", snr);
}

void TestFreeze() {
  printf("freeze keeps the buffer playing after the input stops\n");
  for (int frozen = 0; frozen < 2; ++frozen) {
    Engine engine;
    engine.Init();
    Signals s = Run(
        &engine, 10.0f,
        [](size_t i, int) { return i < 3 * 48000 ? Sine(i, 440.0f, 0.5f) : 0.0f; },
        [=](size_t i, Controls* c) {
          c->density = 0.85f;
          c->size = 0.5f;
          c->dry_wet = 1.0f;
          c->gain = 1;
          c->freeze = frozen && i > size_t(2.5f * kSr);
        });
    const float rms = Rms(s.out_l, 8.0f, 10.0f);
    if (frozen) {
      CHECK(rms > 0.05f, "frozen buffer should keep playing (rms %f)", rms);
      CHECK(engine.status().frozen, "status should report frozen");
    } else {
      CHECK(rms < 0.005f, "unfrozen buffer should empty (rms %f)", rms);
    }
    printf("  freeze=%d: rms 5-7 s after input stopped %.4f\n", frozen, rms);
  }

  // Freeze gate (momentary, as the Beads freeze input).
  Engine engine;
  engine.Init();
  Signals s = Run(
      &engine, 10.0f,
      [](size_t i, int) { return i < 3 * 48000 ? Sine(i, 440.0f, 0.5f) : 0.0f; },
      [](size_t, Controls* c) {
        c->density = 0.85f;
        c->dry_wet = 1.0f;
        c->gain = 1;
      },
      [](size_t i) { return i > size_t(2.5f * kSr); });
  CHECK(Rms(s.out_l, 8.0f, 10.0f) > 0.05f, "freeze gate should freeze");
}

void TestSeedInput() {
  printf("seed gate: clocked grains, patch detection by activity\n");
  Engine engine;
  engine.Init();
  const size_t period = size_t(0.25f * kSr);
  const size_t pulse = size_t(0.005f * kSr);
  const size_t stop = size_t(3.0f * kSr);
  Signals s = Run(
      &engine, 9.0f,
      [](size_t i, int) { return Sine(i, 220.0f, 0.5f); },
      [](size_t, Controls* c) {
        c->density = 0.02f;  // divider = 1: every pulse seeds a grain
        c->size = 0.3f;
        c->dry_wet = 1.0f;
      },
      GateFn(),
      [=](size_t i) { return i < stop && (i % period) < pulse; });

  // Every pulse after the first one (which marks the input as patched)
  // should produce a grain gate within 2.5 ms delay + FIFO latency + 1 ms.
  const size_t max_lag = 120 + Engine::kLatency + 48;
  int pulses = 0, matched = 0;
  for (size_t t = period; t < stop; t += period) {
    ++pulses;
    for (size_t i = t; i < t + max_lag + 1 && i < s.gate.size(); ++i) {
      if (s.gate[i] && !s.gate[i - 1]) {
        ++matched;
        break;
      }
    }
  }
  CHECK(matched >= pulses - 1, "only %d/%d seed pulses produced grains",
        matched, pulses);
  printf("  %d/%d seed pulses produced a grain gate\n", matched, pulses);

  // Unpatched again after the timeout: the internal generator (locked,
  // density 0.02 = regular fast grains) takes over.
  size_t late_gates = 0;
  for (size_t i = size_t(8.0f * kSr); i < s.gate.size(); ++i) {
    late_gates += s.gate[i] && !s.gate[i - 1];
  }
  CHECK(!engine.status().seed_input_patched,
        "seed input should be considered unpatched after 4 s of inactivity");
  CHECK(late_gates > 10, "internal generator should run again (%zu gates)",
        late_gates);
}

void TestSeedButton() {
  printf("seed button: tap unlocks, long press relocks\n");
  Engine engine;
  engine.Init();
  CHECK(engine.status().seed_locked, "starts locked");
  Run(&engine, 0.5f, InputFn(),
      [](size_t i, Controls* c) { c->seed_button = i < 4800; });
  CHECK(!engine.status().seed_locked, "short press unlocks");
  Run(&engine, 2.0f, InputFn(),
      [](size_t i, Controls* c) { c->seed_button = i < 1.7f * 48000; });
  CHECK(engine.status().seed_locked, "1.5 s press relocks");
}

void TestNoInputIsSilent() {
  printf("no input: AGC does not amplify the noise floor\n");
  Engine engine;
  engine.Init();
  Signals s = Run(
      &engine, 4.0f,
      [](size_t, int) { return 0.00005f * Noise(); },
      [](size_t, Controls* c) {
        c->dry_wet = 0.5f;
        c->density = 0.8f;
      });
  const float rms = Rms(s.out_l, 2.5f, 4.0f);
  CHECK(rms < 1e-4f, "unpatched input should give silence (rms %g)", rms);
  CHECK(!engine.status().input_present, "input reported present");
}

void TestQualitySwitching() {
  printf("switching quality/mono/synth while running\n");
  Engine engine;
  engine.Init();
  Signals s = Run(
      &engine, 8.0f,
      [](size_t i, int) { return Sine(i, 220.0f, 0.5f); },
      [](size_t i, Controls* c) {
        const size_t step = i / 9600;  // every 200 ms
        c->quality = step % 4;
        c->mono = (step / 4) % 2;
        c->synth = (step % 11) == 10;
        c->density = 0.8f;
        c->dry_wet = 0.8f;
        c->size = (step % 3) == 0 ? 1.0f : 0.5f;
      });
  CHECK(AllFiniteAndBounded(s), "bad samples while switching");
}

void TestVintageDelayKeepsPitch() {
  printf("vintage digital (32 kHz path): delay mode preserves pitch\n");
  Engine engine;
  engine.Init();
  Signals s = Run(
      &engine, 5.0f,
      [](size_t i, int) { return Sine(i, 1000.0f, 0.5f); },
      [](size_t, Controls* c) {
        c->quality = QUALITY_VINTAGE_DIGITAL;
        c->size = 1.0f;
        c->time = 0.3f;
        c->dry_wet = 1.0f;
        c->gain = 1;
      });
  const float f = ZeroCrossingFrequency(s.out_l, 3.0f, 5.0f);
  CHECK(fabsf(f - 1000.0f) < 10.0f, "frequency %f", f);
  printf("  delayed signal frequency %.1f Hz\n", f);
}

void TestNebulaeMapping() {
  printf("Nebulae control mapping\n");
  CHECK(fabsf(SemitonesFromNebulaePitch(0.6f)) < 1e-4f, "unison at 0.6");
  CHECK(fabsf(SemitonesFromNebulaePitch(0.8f) - 12.0f) < 1e-3f,
        "0.2 per octave");
  CHECK(RandomAmountFromKnob(0.0f, false) == 0.0f, "default = no randomness");
  CHECK(fabsf(RandomAmountFromKnob(1.0f, false) - 1.0f) < 1e-3f, "max = 1");
  CHECK(RandomAmountFromKnob(1.0f, true) < -0.99f, "sparse flavour negative");
  CHECK(ReverbFromNebulaeSpeed(0.4f) == 0.0f, "no reverb below speed 0");
  CHECK(fabsf(ReverbFromNebulaeSpeed(0.625f) - 0.25f) < 1e-4f, "1x = 0.25");
  CHECK(GainFromKnob(0.0f) == 0, "auto gain by default");
  CHECK(GainFromKnob(1.0f) == 255, "max manual gain");
}

}  // namespace

int main() {
  TestNebulaeMapping();
  TestResampler();
  TestDryPathAndLatency();
  TestSeedButton();
  TestNoInputIsSilent();
  TestFreeze();
  TestSeedInput();
  TestVintageDelayKeepsPitch();
  TestQualitySwitching();
  TestAllModesStable();
  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
