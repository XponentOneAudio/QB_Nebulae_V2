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
#include <cstring>
#include <functional>
#include <vector>

#include <unistd.h>

#include <string>

#include "engine/gate_timing.h"
#include "engine/nebulae_mapping.h"
#include "engine/pearls_engine.h"
#include "engine/pearls_files.h"
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
  // As Beads: a 4 s press relocks when released.
  Run(&engine, 4.5f, InputFn(),
      [](size_t i, Controls* c) { c->seed_button = i < 4.3f * 48000; });
  CHECK(engine.status().seed_locked, "4 s press relocks");
  Run(&engine, 0.5f, InputFn(),
      [](size_t i, Controls* c) { c->seed_button = i < 4800; });
  Run(&engine, 2.5f, InputFn(),
      [](size_t i, Controls* c) { c->seed_button = i < 2.0f * 48000; });
  CHECK(!engine.status().seed_locked, "2 s press does not relock");
  // SEED + FREEZE relocks, and that FREEZE press doesn't freeze.
  Run(&engine, 1.0f, InputFn(),
      [](size_t i, Controls* c) {
        c->seed_button = i > 4800 && i < 24000;
        c->freeze = i > 12000;
      });
  CHECK(engine.status().seed_locked, "SEED + FREEZE relocks");
  CHECK(!engine.status().frozen, "the combo's FREEZE press is ignored");
  Run(&engine, 0.2f, InputFn(), [](size_t, Controls* c) { c->freeze = false; });
  CHECK(engine.status().frozen, "the next FREEZE press freezes");
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


// Runs the engine until the current transfer is done (at most `seconds`).
bool FinishTransfer(Engine* engine, const Controls& c, float seconds,
                    std::vector<float>* out = NULL) {
  float silence[64] = { 0 };
  float l[64], r[64];
  for (size_t n = 0; n < size_t(seconds * kSr / 64.0f); ++n) {
    if (engine->transfer_state() == TRANSFER_DONE) {
      return true;
    }
    engine->Process(c, silence, silence, NULL, NULL, l, r, NULL, 64);
    if (out) {
      out->insert(out->end(), l, l + 64);
    }
  }
  return engine->transfer_state() == TRANSFER_DONE;
}

void Idle(Engine* engine, const Controls& c, float seconds) {
  FinishTransfer(engine, c, seconds);
}

const char* kQualityNames[] = {
  "dirty tape", "clean tape", "vintage", "digital"
};

void TestBufferTransfers() {
  printf("loading and saving the buffer\n");
  const float rates[] = { 24000.0f, 24000.0f, 32000.0f, 48000.0f };
  for (int quality = 0; quality < 4; ++quality) {
    for (int mono = 0; mono < 2; ++mono) {
      Engine engine;
      engine.Init();
      Controls c;
      c.SetDefaults();
      c.quality = quality;
      c.mono = mono;
      c.dry_wet = 1.0f;
      c.density = 0.85f;
      Idle(&engine, c, 0.2f);

      // A 2 s chirp at the buffer's own rate.
      const float rate = rates[quality];
      const size_t frames = size_t(2.0f * rate);
      std::vector<float> l(frames), r(frames);
      for (size_t i = 0; i < frames; ++i) {
        const float t = float(i) / rate;
        l[i] = 0.5f * sinf(2.0f * float(M_PI) * (100.0f + 200.0f * t) * t);
        r[i] = mono ? l[i] : 0.4f * sinf(2.0f * float(M_PI) * 300.0f * t);
      }
      CHECK(engine.StartLoad(&l[0], &r[0], frames, rate, true), "start load");
      std::vector<float> during;
      CHECK(FinishTransfer(&engine, c, 2.0f, &during), "%s mono=%d: load "
            "does not finish", kQualityNames[quality], mono);
      engine.EndTransfer();
      CHECK(engine.status().latched_frozen, "frozen after load");

      std::vector<float> saved_l(Engine::MaxBufferFrames());
      std::vector<float> saved_r(Engine::MaxBufferFrames());
      CHECK(engine.StartSave(&saved_l[0], &saved_r[0], saved_l.size()),
            "start save");
      CHECK(FinishTransfer(&engine, c, 2.0f), "save does not finish");
      engine.EndTransfer();
      CHECK(engine.saved_frames() == frames, "%s mono=%d: saved %zu frames "
            "instead of %zu", kQualityNames[quality], mono,
            engine.saved_frames(), frames);
      CHECK(engine.saved_sample_rate() == rate, "saved rate %f",
            engine.saved_sample_rate());
      CHECK(engine.saved_mono() == bool(mono), "saved mono flag");
      double err = 0.0, ref = 0.0;
      for (size_t i = 0; i < std::min(frames, engine.saved_frames()); ++i) {
        const float want_l = mono ? 0.5f * (l[i] + r[i]) : l[i];
        const float want_r = mono ? want_l : r[i];
        err += pow(saved_l[i] - want_l, 2) + pow(saved_r[i] - want_r, 2);
        ref += pow(want_l, 2) + pow(want_r, 2);
      }
      const float snr = 10.0f * log10f(float(ref / std::max(err, 1e-20)));
      // Mu-law 8-bit vs 12/16-bit.
      CHECK(snr > (quality == 0 ? 30.0f : 60.0f), "%s mono=%d: snr %.1f dB",
            kQualityNames[quality], mono, snr);

      // Saving what was saved and loading it back is lossless.
      const size_t n = engine.saved_frames();
      std::vector<float> again_l(saved_l.begin(), saved_l.begin() + n);
      std::vector<float> again_r(saved_r.begin(), saved_r.begin() + n);
      engine.StartLoad(&again_l[0], &again_r[0], n, rate, true);
      FinishTransfer(&engine, c, 2.0f);
      engine.EndTransfer();
      engine.StartSave(&saved_l[0], &saved_r[0], saved_l.size());
      FinishTransfer(&engine, c, 2.0f);
      engine.EndTransfer();
      size_t different = 0;
      for (size_t i = 0; i < n; ++i) {
        different += saved_l[i] != again_l[i] || saved_r[i] != again_r[i];
      }
      CHECK(engine.saved_frames() == n && different == 0,
            "%s mono=%d: round trip changed %zu frames",
            kQualityNames[quality], mono, different);

      // The held buffer plays with no input.
      std::vector<float> out;
      for (int k = 0; k < 2000; ++k) {
        float silence[64] = { 0 }, ol[64], orr[64];
        engine.Process(c, silence, silence, NULL, NULL, ol, orr, NULL, 64);
        out.insert(out.end(), ol, ol + 64);
      }
      const float rms = Rms(out, 1.0f, 2.6f);
      CHECK(rms > 0.03f, "%s mono=%d: loaded buffer silent (%f)",
            kQualityNames[quality], mono, rms);
      printf("  %-10s mono=%d: %zu frames at %.0f Hz, snr %.1f dB, "
             "playback rms %.3f\n", kQualityNames[quality], mono, n, rate,
             snr, rms);
    }
  }

  // Resampling and truncation: 10 s at 44.1 kHz into the 4 s digital buffer.
  Engine engine;
  engine.Init();
  Controls c;
  c.SetDefaults();
  Idle(&engine, c, 0.1f);
  const size_t frames = 441000;
  std::vector<float> l(frames), r(frames);
  for (size_t i = 0; i < frames; ++i) {
    l[i] = r[i] = 0.5f * sinf(2.0f * float(M_PI) * 1000.0f * i / 44100.0f);
  }
  engine.StartLoad(&l[0], &r[0], frames, 44100.0f, true);
  FinishTransfer(&engine, c, 2.0f);
  engine.EndTransfer();
  std::vector<float> saved_l(Engine::MaxBufferFrames());
  std::vector<float> saved_r(Engine::MaxBufferFrames());
  engine.StartSave(&saved_l[0], &saved_r[0], saved_l.size());
  FinishTransfer(&engine, c, 2.0f);
  engine.EndTransfer();
  saved_l.resize(engine.saved_frames());
  CHECK(engine.saved_frames() > 190000 && engine.saved_frames() < 192000,
        "digital stereo buffer holds 4 s: %zu", engine.saved_frames());
  const float f = ZeroCrossingFrequency(saved_l, 0.5f, 3.5f);
  CHECK(fabsf(f - 1000.0f) < 2.0f, "1 kHz after 44.1k -> 48k: %f", f);
  printf("  44.1 kHz sample: %zu frames kept, %.1f Hz\n",
         engine.saved_frames(), f);
}

void TestFreezeProtectsBuffer() {
  printf("quality, mono and synth changes wait while frozen (as Beads)\n");
  Engine engine;
  engine.Init();
  Controls c;
  c.SetDefaults();
  c.dry_wet = 1.0f;
  c.density = 0.85f;
  c.time = 0.1f;
  c.gain = 1;
  Run(&engine, 2.0f, [](size_t i, int) { return Sine(i, 440.0f, 0.4f); },
      [&](size_t, Controls* x) { *x = c; });
  c.freeze = true;
  Idle(&engine, c, 0.1f);
  const uint32_t generation = engine.buffer_generation();
  c.quality = QUALITY_DIRTY_TAPE;
  c.mono = true;
  std::vector<float> out;
  FinishTransfer(&engine, c, 2.0f, &out);
  CHECK(engine.buffer_generation() == generation, "buffer kept while frozen");
  CHECK(Rms(out, 1.0f, 2.0f) > 0.05f, "still playing (%f)",
        Rms(out, 1.0f, 2.0f));
  std::vector<float> saved_l(Engine::MaxBufferFrames());
  std::vector<float> saved_r(Engine::MaxBufferFrames());
  engine.StartSave(&saved_l[0], &saved_r[0], saved_l.size());
  FinishTransfer(&engine, c, 1.0f);
  engine.EndTransfer();
  CHECK(engine.saved_sample_rate() == 48000.0f && !engine.saved_mono(),
        "still digital stereo while frozen");
  c.freeze = false;
  Idle(&engine, c, 0.2f);
  engine.StartSave(&saved_l[0], &saved_r[0], saved_l.size());
  FinishTransfer(&engine, c, 1.0f);
  engine.EndTransfer();
  CHECK(engine.saved_sample_rate() == 24000.0f && engine.saved_mono(),
        "applied once unfrozen");
}

void TestGainRestart() {
  printf("automatic gain control restart\n");
  Engine engine;
  engine.Init();
  Controls c;
  c.SetDefaults();
  Run(&engine, 8.0f, [](size_t i, int) { return Sine(i, 440.0f, 0.1f); },
      [&](size_t, Controls* x) { *x = c; });
  CHECK(!engine.status().gain_adjusting, "settled after 8 s");
  engine.RestartGainControl();
  Run(&engine, 1.0f, [](size_t i, int) { return Sine(i, 440.0f, 0.1f); },
      [&](size_t, Controls* x) { *x = c; });
  CHECK(engine.status().gain_adjusting, "measuring again after a restart");
}

void TestHoldRelease() {
  printf("a loaded buffer is frozen as if FREEZE were pressed\n");
  for (int freeze_first = 0; freeze_first < 2; ++freeze_first) {
    Engine engine;
    engine.Init();
    Controls c;
    c.SetDefaults();
    c.freeze = freeze_first;
    Idle(&engine, c, 0.1f);
    std::vector<float> l(48000, 0.25f), r(48000, 0.25f);
    engine.StartLoad(&l[0], &r[0], l.size(), 48000.0f, true);
    FinishTransfer(&engine, c, 1.0f);
    engine.EndTransfer();
    Idle(&engine, c, 0.1f);
    CHECK(engine.status().frozen && engine.status().latched_frozen,
          "frozen after load");
    const uint32_t generation = engine.buffer_generation();
    Idle(&engine, c, 0.5f);
    CHECK(engine.buffer_generation() == generation, "buffer unchanged");
    c.freeze = !c.freeze;  // one press releases it
    Idle(&engine, c, 0.1f);
    CHECK(!engine.status().frozen, "freeze_first=%d: released", freeze_first);
    CHECK(engine.buffer_generation() != generation,
          "recording again changes the buffer");
    c.freeze = !c.freeze;  // the next one freezes
    Idle(&engine, c, 0.1f);
    CHECK(engine.status().frozen, "freeze_first=%d: frozen again",
          freeze_first);
  }
}

void TestTapTempo() {
  printf("tap tempo\n");
  Engine engine;
  engine.Init();
  // Taps every 0.4 s from 1 s to 2.2 s; held for 1.2 s at 6 s.
  Signals s = Run(
      &engine, 8.0f,
      [](size_t i, int) { return Sine(i, 220.0f, 0.3f); },
      [](size_t i, Controls* c) {
        c->density = 0.75f;
        c->dry_wet = 1.0f;
        c->gain = 1;
        const float t = float(i) / kSr;
        bool tap = false;
        for (int k = 0; k < 4; ++k) {
          const float start = 1.0f + 0.4f * k;
          tap = tap || (t >= start && t < start + 0.05f);
        }
        c->tap = tap || (t >= 6.0f && t < 7.2f);
      });
  (void) s;
  CHECK(!engine.status().clock_running, "clock stopped by a long press");

  // Grain gates follow the clock.
  Engine engine2;
  engine2.Init();
  std::vector<size_t> beats;
  bool running = false;
  bool previous_led = false;
  Signals s2 = Run(
      &engine2, 6.0f,
      [](size_t i, int) { return Sine(i, 220.0f, 0.3f); },
      [&](size_t i, Controls* c) {
        // With a clock, density right of noon is the probability of a grain
        // on each beat: fully clockwise = every beat.
        c->density = 1.0f;
        c->dry_wet = 1.0f;
        c->gain = 1;
        const float t = float(i) / kSr;
        bool tap = false;
        for (int k = 0; k < 3; ++k) {
          const float start = 0.5f + 0.25f * k;
          tap = tap || (t >= start && t < start + 0.03f);
        }
        c->tap = tap;
        running = running || engine2.status().clock_running;
        const bool led = engine2.status().clock_led;
        if (led && !previous_led) {
          beats.push_back(i);
        }
        previous_led = led;
      });
  CHECK(running && engine2.status().clock_running, "clock running");
  CHECK(engine2.status().seed_input_patched, "clock drives the seed input");
  size_t gates = 0;
  for (size_t i = size_t(3.0f * kSr); i < s2.gate.size(); ++i) {
    gates += s2.gate[i] && !s2.gate[i - 1];
  }
  // 3 s at 4 beats per second.
  CHECK(gates >= 10 && gates <= 14, "%zu grains in 3 s at 240 BPM", gates);
  float interval = 0.0f;
  if (beats.size() > 4) {
    interval = float(beats.back() - beats[beats.size() - 4]) / 3.0f / kSr;
  }
  CHECK(fabsf(interval - 0.25f) < 0.005f, "beat interval %f s", interval);
  printf("  %zu grains in 3 s, beat every %.3f s\n", gates, interval);
}

void TestNewControlsStable() {
  printf("reverse and stereo spread in all modes\n");
  for (int quality = 0; quality < 4; ++quality) {
    for (int synth = 0; synth < 2; ++synth) {
      Engine engine;
      engine.Init();
      Signals s = Run(
          &engine, 3.0f,
          [](size_t i, int ch) { return Sine(i, ch ? 330.0f : 220.0f, 0.4f); },
          [=](size_t, Controls* c) {
            c->quality = quality;
            c->synth = synth;
            c->density = 0.9f;
            c->size = 0.6f;
            c->time = 0.05f;  // tape buffers are long: read recent audio
            c->dry_wet = 1.0f;
            c->reverse = 1.0f;
            c->spread = 1.0f;
            c->gain = 1;
          });
      CHECK(AllFiniteAndBounded(s), "q%d synth=%d: bad samples", quality,
            synth);
      CHECK(Rms(s.out_l, 1.0f, 3.0f) > 0.01f, "q%d synth=%d: silent",
            quality, synth);
    }
  }
  // Reversed grains play a rising sawtooth backwards.
  for (int reverse = 0; reverse < 2; ++reverse) {
    Engine engine;
    engine.Init();
    Controls c;
    c.SetDefaults();
    c.density = 0.8f;
    c.size = 0.5f;
    c.shape = 0.5f;
    c.dry_wet = 1.0f;
    c.reverse = reverse;
    Idle(&engine, c, 0.1f);
    std::vector<float> l(96000), r(96000);
    for (size_t i = 0; i < l.size(); ++i) {
      l[i] = r[i] = 0.5f * (float(i % 480) / 480.0f - 0.5f);
    }
    engine.StartLoad(&l[0], &r[0], l.size(), 48000.0f, true);
    FinishTransfer(&engine, c, 1.0f);
    engine.EndTransfer();
    std::vector<float> out;
    for (int k = 0; k < 1500; ++k) {
      float silence[64] = { 0 }, ol[64], orr[64];
      engine.Process(c, silence, silence, NULL, NULL, ol, orr, NULL, 64);
      out.insert(out.end(), ol, ol + 64);
    }
    size_t up = 0, down = 0;
    for (size_t i = 24000; i < out.size(); ++i) {
      const float d = out[i] - out[i - 1];
      up += d > 0.0f && d < 0.01f;
      down += d < 0.0f && d > -0.01f;
    }
    CHECK(reverse ? down > 3 * up : up > 3 * down,
          "reverse=%d: %zu rising, %zu falling steps", reverse, up, down);
    printf("  reverse=%d: %zu rising, %zu falling steps\n", reverse, up, down);
  }

  // With full spread, left and right grains differ even for a mono input.
  Engine engine;
  engine.Init();
  Signals s = Run(
      &engine, 3.0f,
      [](size_t i, int) { return Sine(i, 220.0f, 0.4f); },
      [](size_t, Controls* c) {
        c->density = 0.9f;
        c->dry_wet = 1.0f;
        c->spread = 1.0f;
        c->gain = 1;
      });
  double diff = 0.0, sum = 0.0;
  for (size_t i = size_t(kSr); i < s.out_l.size(); ++i) {
    diff += pow(s.out_l[i] - s.out_r[i], 2);
    sum += pow(s.out_l[i] + s.out_r[i], 2);
  }
  CHECK(diff > 0.05 * sum, "spread: side/mid energy %f", diff / sum);
  printf("  spread: side/mid energy ratio %.2f\n", diff / sum);
}

void TestFiles() {
  printf("files\n");
  char dir_template[] = "/tmp/pearls_test_XXXXXX";
  const std::string dir = mkdtemp(dir_template);
  std::vector<float> l(1000), r(1000);
  for (size_t i = 0; i < l.size(); ++i) {
    l[i] = float(int(i) - 500) / 1024.0f;
    r[i] = -l[i];
  }
  for (int mono = 0; mono < 2; ++mono) {
    const std::string path = dir + (mono ? "/b.wav" : "/a.wav");
    std::string error;
    CHECK(WriteWavFile(path, &l[0], &r[0], l.size(), 32000, mono, &error),
          "write: %s", error.c_str());
    AudioData d;
    CHECK(ReadWavFile(path, 100000, &d, &error), "read: %s", error.c_str());
    CHECK(d.frames() == l.size() && d.sample_rate == 32000.0f, "format");
    size_t different = 0;
    for (size_t i = 0; i < d.frames(); ++i) {
      different += d.l[i] != l[i] || d.r[i] != (mono ? l[i] : r[i]);
    }
    CHECK(different == 0, "mono=%d: %zu frames differ", mono, different);
    CHECK(ReadWavFile(path, 10, &d, &error) && d.frames() == 10,
          "max_frames");
  }
  // Other encodings: 8-bit, 24-bit, float, 4 channels.
  {
    FILE* fp = fopen((dir + "/c.wav").c_str(), "wb");
    const unsigned char header[] = {
      'R','I','F','F', 44,0,0,0, 'W','A','V','E',
      'f','m','t',' ', 16,0,0,0, 1,0, 4,0, 0x44,0xac,0,0, 0,0,0,0, 12,0, 24,0,
      'd','a','t','a', 12,0,0,0,
      0,0,0x40, 0,0,0xc0, 1,2,3, 4,5,6 };
    fwrite(header, 1, sizeof(header), fp);
    fclose(fp);
    AudioData d;
    std::string error;
    CHECK(ReadWavFile(dir + "/c.wav", 100, &d, &error), "24-bit: %s",
          error.c_str());
    CHECK(d.frames() == 1 && d.l[0] == 0.5f && d.r[0] == -0.5f &&
          d.sample_rate == 44100.0f, "24-bit 4-channel values %f %f",
          d.l[0], d.r[0]);
  }
  {
    FILE* fp = fopen((dir + "/._d.wav").c_str(), "wb");
    fputs("AppleDouble", fp);
    fclose(fp);
    fp = fopen((dir + "/E.WAV").c_str(), "wb");
    fclose(fp);
    fp = fopen((dir + "/f.txt").c_str(), "wb");
    fclose(fp);
  }
  std::vector<std::string> files = ListWavFiles(dir);
  CHECK(files.size() == 4 && files[0] == dir + "/E.WAV" &&
        files[1] == dir + "/a.wav", "list: %zu files", files.size());
  AudioData d;
  std::string error;
  CHECK(!ReadWavFile(dir + "/E.WAV", 100, &d, &error), "empty file fails");
  CHECK(NextFreeFileName(dir, "a") == dir + "/a001.wav", "free name");

  SavedState state;
  state.quality = 1;
  state.frozen = true;
  state.load_index = 7;
  CHECK(WriteSavedState(dir + "/state.txt", state), "write state");
  SavedState read;
  CHECK(ReadSavedState(dir + "/state.txt", &read) && read.quality == 1 &&
        read.frozen && read.load_index == 7, "read state");
  CHECK(!ReadSavedState(dir + "/none.txt", &read), "missing state");
  CHECK(MakeDirectories(dir + "/x/y/z"), "mkdir -p");
  bool mounted = true;
  setenv("PEARLS_ROOT", dir.c_str(), 1);
  Paths paths = Paths::Default();
  CHECK(!paths.mount_usb && paths.state_dir == dir + "/home/alarm/pearls",
        "paths under PEARLS_ROOT");
  CHECK(!MountUsb(paths, &mounted, &error) && !mounted, "no stick dir");
  MakeDirectories(paths.usb_dir);
  CHECK(MountUsb(paths, &mounted, &error) && !mounted, "stick dir");
  unsetenv("PEARLS_ROOT");
  const std::string command = "rm -rf " + dir;
  CHECK(system(command.c_str()) == 0, "cleanup");
}

void TestRenderClock() {
  printf("gate timing across bursty rendering\n");
  // Csound renders 4 cycles of 64 frames in a burst every 256 frames, with
  // up to 2 ms of scheduling jitter; edges happen at random times and are
  // seen by a 0.2 ms polling thread.
  const double sr = 48000.0;
  RenderClock clock;
  clock.Init(sr, 0.0004 + 64.0 / sr);
  srand(1);
  const double latency = 0.005;  // render time ahead of playback
  double max_error = 0.0;
  double min_offset = 1e9, max_offset = -1e9;
  size_t late = 0;
  std::vector<double> pending;  // edge times not placed yet
  double next_edge = 0.1;
  for (uint64_t frame = 0; frame < uint64_t(20 * sr); frame += 64) {
    const uint64_t burst = frame / 256 * 256;
    const double jitter = 0.002 * (rand() / double(RAND_MAX));
    // The burst starts when the hardware needs it, the cycles of a burst
    // are rendered back to back.
    const double now = burst / sr - latency + jitter + (frame - burst) /
        sr * 0.1;
    clock.Observe(now, frame);
    while (next_edge < now - 0.0002) {
      pending.push_back(next_edge);
      next_edge += 0.01 + 0.03 * (rand() / double(RAND_MAX));
    }
    std::vector<double> still;
    for (size_t i = 0; i < pending.size(); ++i) {
      const double position = clock.InputFrame(pending[i]);
      if (position >= double(frame + 64)) {
        still.push_back(pending[i]);
        continue;
      }
      if (position < double(frame)) {
        ++late;
      }
      if (frame > uint64_t(3 * sr)) {
        // Constant latency: position in time minus the edge time.
        const double offset = position / sr - pending[i];
        min_offset = std::min(min_offset, offset);
        max_offset = std::max(max_offset, offset);
      }
    }
    pending.swap(still);
    // Output: an edge at this frame must be scheduled after now.
    const double t = clock.OutputTime(double(frame));
    if (frame > uint64_t(3 * sr)) {
      max_error = std::max(max_error, now - t);
    }
  }
  const double spread = max_offset - min_offset;
  CHECK(late == 0, "%zu input edges placed in an earlier cycle", late);
  CHECK(spread < 0.0005, "input timing spread %.3f ms", spread * 1000.0);
  CHECK(max_error <= 0.0, "output edges scheduled in the past (%f)",
        max_error);
  printf("  input edge timing spread %.3f ms (was up to %.1f ms)\n",
         spread * 1000.0, 256.0 / sr * 1000.0);

  SpscQueue<int, 4> q;
  for (int i = 0; i < 4; ++i) {
    CHECK(q.Push(i), "push");
  }
  CHECK(!q.Push(5), "full");
  CHECK(q.Peek() && *q.Peek() == 0, "fifo order");
  q.Pop();
  CHECK(q.Push(5), "push after pop");
}

}  // namespace

int main() {
  TestNebulaeMapping();
  TestResampler();
  TestDryPathAndLatency();
  TestSeedButton();
  TestNoInputIsSilent();
  TestFreeze();
  TestFreezeProtectsBuffer();
  TestGainRestart();
  TestSeedInput();
  TestVintageDelayKeepsPitch();
  TestQualitySwitching();
  TestAllModesStable();
  TestBufferTransfers();
  TestHoldRelease();
  TestTapTempo();
  TestNewControlsStable();
  TestFiles();
  TestRenderClock();
  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
