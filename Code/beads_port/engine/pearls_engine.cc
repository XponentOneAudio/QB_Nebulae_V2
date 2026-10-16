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
// Pearls engine. The control flow follows beads/beads.cc (Process() and
// MonitorStateChange()) and the seed button logic of beads/ui.cc.

#include "engine/pearls_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pearls {

using namespace beads;
using namespace stmlib;
using namespace std;

namespace {

// Same memory layout as the Beads firmware: 512 kB + 256 kB of delay memory
// split in three 256 kB regions, and 64 kB of reverb memory.
const size_t kDelayRegionSize = 256 * 1024;
const size_t kReverbBufferSize = 32768;

const float kSeedLongPressTime = 1.5f;      // s, seed button relocks
const float kSeedPatchTimeout = 4.0f;       // s without edges = unpatched
const size_t kSeedDelay = 120;              // 2.5 ms at 48 kHz, as Beads
const float kFadeOutTime = 0.01f;           // s, before reconfiguring
const float kFadeInTime = 0.05f;            // s, after reconfiguring
const float kSeedLedTime = 0.04f;           // s

// Input presence detection, replacing the Beads normalisation probe: input
// is considered unpatched after 2 s below -66 dBFS, and patched again as soon
// as the level exceeds -60 dBFS.
const float kInputPresentThreshold = 0.001f;
const float kInputAbsentThreshold = 0.0005f;
const float kInputAbsentTime = 2.0f;        // s

// Intermediate rate for the 48k <-> 32k resamplers is 96 kHz.
const double kResamplerCutoff = 14500.0 / 96000.0;
const double kResamplerKaiserBeta = 7.0;

inline size_t Seconds(float t) {
  return size_t(t * float(Engine::kSampleRate));
}

GainControl::SoftClippingMode SoftClippingModeForQuality(int quality) {
  switch (quality) {
    case QUALITY_DIRTY_TAPE:
      return GainControl::SOFT_CLIPPING_MODE_ON;
    case QUALITY_CLEAN_TAPE:
      return GainControl::SOFT_CLIPPING_MODE_DYNAMIC;
    default:
      return GainControl::SOFT_CLIPPING_MODE_OFF;
  }
}

}  // namespace

void Controls::SetDefaults() {
  time = 0.5f;
  size = 0.5f;
  shape = 0.5f;
  density = 0.75f;
  pitch = 0.0f;
  time_random = size_random = shape_random = pitch_random = 0.0f;
  feedback = 0.0f;
  dry_wet = 0.5f;
  reverb = 0.0f;
  quality = QUALITY_DIGITAL;
  mono = false;
  synth = false;
  freeze = false;
  seed_button = false;
  gain = 0;
}

Engine::~Engine() {
  delete processor_;
  delete synth_;
  delete[] delay_buffer_;
  delete[] reverb_buffer_;
}

bool Engine::Init() {
  delay_buffer_ = new uint8_t[3 * kDelayRegionSize];
  reverb_buffer_ = new uint16_t[kReverbBufferSize];
  processor_ = new Processor;
  synth_ = new Synth;
  memset(delay_buffer_, 0, 3 * kDelayRegionSize);
  memset(reverb_buffer_, 0, kReverbBufferSize * sizeof(uint16_t));

  uint8_t* buffers[3] = {
    delay_buffer_,
    delay_buffer_ + kDelayRegionSize,
    delay_buffer_ + 2 * kDelayRegionSize
  };
  processor_->Init(buffers, reverb_buffer_);
  synth_->Init(reverb_buffer_);

  down_.Init(kResamplerCutoff, kResamplerKaiserBeta);
  up_.Init(kResamplerCutoff, kResamplerKaiserBeta);

  controls_.SetDefaults();
  memset(&status_, 0, sizeof(status_));

  fifo_index_ = 0;
  fill(&in_fifo_l_[0], &in_fifo_l_[kBlockSize], 0.0f);
  fill(&in_fifo_r_[0], &in_fifo_r_[kBlockSize], 0.0f);
  fill(&out_fifo_l_[0], &out_fifo_l_[kBlockSize], 0.0f);
  fill(&out_fifo_r_[0], &out_fifo_r_[kBlockSize], 0.0f);
  fill(&freeze_fifo_[0], &freeze_fifo_[kBlockSize], false);
  fill(&seed_fifo_[0], &seed_fifo_[kBlockSize], false);
  fill(&gate_fifo_[0], &gate_fifo_[kBlockSize], false);

  synth_mode_ = controls_.synth;
  target_quality_ = controls_.quality;
  target_mono_ = controls_.mono;
  target_synth_ = controls_.synth;
  fade_ = 0.0f;
  reconfigure_pending_ = false;
  reset_gain_pending_ = true;
  gain_control_.Init(
      float(kSampleRate), SoftClippingModeForQuality(target_quality_), true);
  ApplyConfiguration();

  seed_locked_ = true;
  seed_pressed_ = false;
  seed_long_press_handled_ = false;
  seed_press_duration_ = 0;

  seed_gate_previous_ = false;
  seed_idle_time_ = 0;
  fill(&seed_delay_line_[0], &seed_delay_line_[128], false);
  seed_delay_write_ = 0;
  seed_flags_previous_ = GATE_FLAG_LOW;
  seed_flags_previous_32k_ = GATE_FLAG_LOW;

  input_envelope_ = 0.0f;
  input_silence_time_ = Seconds(kInputAbsentTime);
  status_.input_present = false;

  seed_led_time_ = 0;
  status_.seed_locked = true;
  status_.overwrite_reserve = 1.0f;
  return true;
}

void Engine::Process(
    const Controls& controls,
    const float* in_l,
    const float* in_r,
    const uint8_t* freeze_gate,
    const uint8_t* seed_gate,
    float* out_l,
    float* out_r,
    uint8_t* gate_out,
    size_t size) {
  controls_ = controls;
  for (size_t i = 0; i < size; ++i) {
    out_l[i] = out_fifo_l_[fifo_index_];
    out_r[i] = out_fifo_r_[fifo_index_];
    if (gate_out) {
      gate_out[i] = gate_fifo_[fifo_index_] ? 1 : 0;
    }
    in_fifo_l_[fifo_index_] = in_l ? in_l[i] : 0.0f;
    in_fifo_r_[fifo_index_] = in_r ? in_r[i] : 0.0f;
    freeze_fifo_[fifo_index_] = freeze_gate ? freeze_gate[i] != 0 : false;
    seed_fifo_[fifo_index_] = seed_gate ? seed_gate[i] != 0 : false;
    if (++fifo_index_ == kBlockSize) {
      RenderBlock();
      fifo_index_ = 0;
    }
  }
}

void Engine::UpdateSeedButton(bool pressed) {
  // beads/ui.cc: pressing SEED unlocks the generator and seeds while held;
  // a long press locks it again.
  if (pressed && !seed_pressed_) {
    seed_locked_ = false;
    seed_press_duration_ = 0;
    seed_long_press_handled_ = false;
  }
  if (pressed) {
    seed_press_duration_ += kBlockSize;
    if (!seed_long_press_handled_ &&
        seed_press_duration_ >= Seconds(kSeedLongPressTime)) {
      seed_locked_ = true;
      seed_long_press_handled_ = true;
    }
  }
  seed_pressed_ = pressed;
}

void Engine::UpdateConfiguration() {
  int quality = controls_.quality;
  CONSTRAIN(quality, 0, QUALITY_LAST - 1);
  const bool mono = controls_.mono;
  const bool synth = controls_.synth;

  // In synth mode the quality setting only selects the wavetable resolution,
  // which the synth reads directly: no reconfiguration needed (as Beads).
  const bool needs_reconfiguration = synth != synth_mode_ || (!synth && (
      Processor::Media(quality) != processor_->media() ||
      mono != processor_->mono()));

  if (needs_reconfiguration) {
    if (!reconfigure_pending_) {
      reset_gain_pending_ = mono != processor_->mono() || synth_mode_;
    }
    reconfigure_pending_ = true;
    target_quality_ = quality;
    target_mono_ = mono;
    target_synth_ = synth;
  } else {
    reconfigure_pending_ = false;
  }

  if (reconfigure_pending_) {
    fade_ -= float(kBlockSize) / (kFadeOutTime * kSampleRate);
    if (fade_ <= 0.0f) {
      fade_ = 0.0f;
      ApplyConfiguration();
      reconfigure_pending_ = false;
    }
  } else {
    fade_ += float(kBlockSize) / (kFadeInTime * kSampleRate);
    if (fade_ > 1.0f) {
      fade_ = 1.0f;
    }
  }
}

void Engine::ApplyConfiguration() {
  synth_mode_ = target_synth_;
  if (!synth_mode_) {
    processor_->Configure(Processor::Media(target_quality_), target_mono_);
    down_.Reset();
    up_.Reset();
  }
  gain_control_.Init(
      float(kSampleRate),
      SoftClippingModeForQuality(target_quality_),
      reset_gain_pending_);
  reset_gain_pending_ = false;
  seed_flags_previous_32k_ = GATE_FLAG_LOW;
}

void Engine::DetectInput(const FloatFrame* in, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    const float peak = max(fabsf(in[i].l), fabsf(in[i].r));
    input_envelope_ = peak > input_envelope_
        ? peak
        : input_envelope_ * 0.9995f;
  }
  if (input_envelope_ > kInputPresentThreshold) {
    status_.input_present = true;
    input_silence_time_ = 0;
  } else if (input_envelope_ < kInputAbsentThreshold) {
    input_silence_time_ += size;
    if (input_silence_time_ >= Seconds(kInputAbsentTime)) {
      input_silence_time_ = Seconds(kInputAbsentTime);
      status_.input_present = false;
    }
  }
}

void Engine::BuildParameters(
    Parameters* p,
    ModulatedParameter* d,
    Processor::MixParameters* m) const {
  const Controls& c = controls_;

  // With no CV "patched" (Nebulae sums CVs into the control values before
  // they reach us), the attenuverters set the amount of internal randomness.
  p->time.base = c.time;
  p->time.attenuverter = c.time_random;
  p->time.cv = 0.0f;
  p->time.has_cv = false;

  p->size.base = c.size;
  p->size.attenuverter = c.size_random;
  p->size.cv = 0.0f;
  p->size.has_cv = false;

  p->shape.base = c.shape;
  p->shape.attenuverter = c.shape_random;
  p->shape.cv = 0.0f;
  p->shape.has_cv = false;

  p->pitch.base = c.pitch;
  p->pitch.attenuverter = c.pitch_random;
  p->pitch.cv = 0.0f;
  p->pitch.has_cv = false;

  p->monophonic_mode = false;

  d->base = c.density;
  d->cv = 0.0f;

  m->feedback = c.feedback;
  m->dry_wet = c.dry_wet;
  m->reverb_amount = c.reverb;
  CONSTRAIN(m->feedback, 0.0f, 1.0f);
  CONSTRAIN(m->dry_wet, 0.0f, 1.0f);
  CONSTRAIN(m->reverb_amount, 0.0f, 1.0f);
  m->output_gate = false;
}

void Engine::ProcessAudio(
    SeedButtonState seed_state,
    const GateFlags* seed,
    const bool* freeze,
    const ModulatedParameter& d,
    const Parameters& p,
    const Processor::MixParameters& m,
    FloatFrame* in,
    FloatFrame* out,
    bool* gate) {
  if (processor_->sample_rate() != 32000.0f) {
    processor_->set_gate_output(gate);
    processor_->Process(seed_state, seed, freeze, d, p, m, in, out, kBlockSize);
    return;
  }

  // Vintage digital: run the processor at 32 kHz, 8 frames per block.
  const size_t n = kBlockSize * 2 / 3;
  float l[kBlockSize], r[kBlockSize];
  float l32[n], r32[n];
  for (size_t i = 0; i < kBlockSize; ++i) {
    l[i] = in[i].l;
    r[i] = in[i].r;
  }
  down_.Process(l, r, l32, r32, kBlockSize);

  FloatFrame in32[n], out32[n];
  bool freeze32[n];
  bool gate32[n];
  GateFlags seed32[n];
  for (size_t j = 0; j < n; ++j) {
    in32[j].l = l32[j];
    in32[j].r = r32[j];
    // 48 kHz frames covered by this 32 kHz frame.
    const size_t first = j * kBlockSize / n;
    const size_t last = (j + 1) * kBlockSize / n;
    bool f = false;
    bool s = false;
    for (size_t i = first; i < last; ++i) {
      f = f || freeze[i];
      if (seed) {
        s = s || (seed[i] & GATE_FLAG_HIGH);
      }
    }
    freeze32[j] = f;
    seed32[j] = seed_flags_previous_32k_ = ExtractGateFlags(
        seed_flags_previous_32k_, s);
  }
  if (!seed) {
    seed_flags_previous_32k_ = GATE_FLAG_LOW;
  }

  processor_->set_gate_output(gate32);
  processor_->Process(
      seed_state, seed ? seed32 : NULL, freeze32, d, p, m, in32, out32, n);

  for (size_t j = 0; j < n; ++j) {
    l32[j] = out32[j].l;
    r32[j] = out32[j].r;
  }
  up_.Process(l32, r32, l, r, n);
  for (size_t i = 0; i < kBlockSize; ++i) {
    out[i].l = l[i];
    out[i].r = r[i];
    gate[i] = gate32[i * n / kBlockSize];
  }
}

void Engine::RenderBlock() {
  UpdateSeedButton(controls_.seed_button);
  UpdateConfiguration();

  FloatFrame in[kBlockSize];
  FloatFrame out[kBlockSize];
  bool freeze[kBlockSize];
  GateFlags seed[kBlockSize];
  bool gate[kBlockSize];

  bool freeze_block = false;
  for (size_t i = 0; i < kBlockSize; ++i) {
    in[i].l = in_fifo_l_[i];
    in[i].r = in_fifo_r_[i];
    freeze[i] = controls_.freeze || freeze_fifo_[i];
    freeze_block = freeze_block || freeze[i];
    gate[i] = false;
  }

  // Seed gate. Beads only uses its seed input when a cable is plugged in;
  // here, an edge marks the input as patched until it stays idle for a while.
  // As on Beads, the gate is delayed by 2.5 ms so that a CV changing at the
  // same time as the trigger is taken into account.
  for (size_t i = 0; i < kBlockSize; ++i) {
    const bool g = seed_fifo_[i];
    if (g) {
      if (!seed_gate_previous_) {
        status_.seed_input_patched = true;
      }
      seed_idle_time_ = 0;
    }
    seed_gate_previous_ = g;
    seed_delay_line_[seed_delay_write_] = g;
    const bool delayed = seed_delay_line_[
        (seed_delay_write_ + 128 - kSeedDelay) % 128];
    seed_delay_write_ = (seed_delay_write_ + 1) % 128;
    seed[i] = seed_flags_previous_ = ExtractGateFlags(
        seed_flags_previous_, delayed);
  }
  seed_idle_time_ += kBlockSize;
  if (seed_idle_time_ >= Seconds(kSeedPatchTimeout)) {
    seed_idle_time_ = Seconds(kSeedPatchTimeout);
    status_.seed_input_patched = false;
  }
  const GateFlags* seed_input = status_.seed_input_patched ? seed : NULL;

  const SeedButtonState seed_state = seed_locked_
      ? SEED_BUTTON_STATE_LOCKED
      : (seed_pressed_ && !seed_long_press_handled_
            ? SEED_BUTTON_STATE_ON
            : SEED_BUTTON_STATE_OFF);

  Parameters p;
  ModulatedParameter d;
  Processor::MixParameters m;
  BuildParameters(&p, &d, &m);

  if (synth_mode_) {
    int quality = controls_.quality;
    CONSTRAIN(quality, 0, QUALITY_LAST - 1);
    synth_->set_gate_output(gate);
    synth_->Render(
        seed_state, seed_input, freeze,
        Synth::Resolution(quality),
        d, p, m, out, kBlockSize);
  } else {
    DetectInput(in, kBlockSize);
    const bool absent = !status_.input_present;
    gain_control_.Process(
        fade_ >= 1.0f,
        controls_.gain,
        absent,
        absent || controls_.mono,
        in, in, kBlockSize);
    ProcessAudio(seed_state, seed_input, freeze, d, p, m, in, out, gate);
  }

  // Fade around reconfigurations, then hand over to the output FIFO.
  bool any_gate = false;
  for (size_t i = 0; i < kBlockSize; ++i) {
    float l = out[i].l * fade_;
    float r = out[i].r * fade_;
    CONSTRAIN(l, -1.0f, 1.0f);
    CONSTRAIN(r, -1.0f, 1.0f);
    out_fifo_l_[i] = l;
    out_fifo_r_[i] = r;
    gate_fifo_[i] = gate[i];
    any_gate = any_gate || gate[i];
  }

  if (any_gate || (seed_pressed_ && !seed_locked_)) {
    seed_led_time_ = Seconds(kSeedLedTime);
  } else if (seed_led_time_ > kBlockSize) {
    seed_led_time_ -= kBlockSize;
  } else {
    seed_led_time_ = 0;
  }

  status_.seed_locked = seed_locked_;
  status_.seed_led = seed_led_time_ > 0;
  status_.frozen = freeze_block;
  status_.overwrite_reserve = synth_mode_
      ? 1.0f
      : processor_->overwrite_reserve();
  status_.vu_meter = gain_control_.vu_meter();
}

}  // namespace pearls
