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
// Pearls: the Beads granular processor as a host-independent engine.
//
// This class replaces the hardware glue of beads/beads.cc (codec callback,
// normalisation detection, UI state machine) with logic that works from plain
// control values and per-sample gate arrays. Hosts (Csound opcode, desktop
// renderer, tests) feed it 48 kHz stereo audio in blocks of any size.

#ifndef PEARLS_ENGINE_PEARLS_ENGINE_H_
#define PEARLS_ENGINE_PEARLS_ENGINE_H_

#include <cstddef>
#include <cstdint>

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"

#include "engine/gain_control.h"
#include "engine/rational_resampler.h"

namespace pearls {

enum Quality {
  QUALITY_DIRTY_TAPE,       // Beads "media" 0: 8-bit mu-law, wow & flutter
  QUALITY_CLEAN_TAPE,       // 1: 12-bit at 24 kHz
  QUALITY_VINTAGE_DIGITAL,  // 2: 12-bit at 32 kHz
  QUALITY_DIGITAL,          // 3: 16-bit at 48 kHz
  QUALITY_LAST
};

// All values are what the Beads firmware computes from its pots and CVs,
// before they reach beads::Processor. See nebulae_mapping.h for the
// conversion from Nebulae control values.
struct Controls {
  float time;          // 0..1
  float size;          // 0..1 (> 0.99 = delay mode)
  float shape;         // 0..1
  float density;       // 0..1 (0.5 = no grains)
  float pitch;         // semitones

  // Internal randomisation ("attenuverter" with no CV patched), -1..+1.
  // > 0: uniform random variation per grain. < 0: sparse random jumps.
  float time_random;
  float size_random;
  float shape_random;
  float pitch_random;

  float feedback;      // 0..1
  float dry_wet;       // 0..1
  float reverb;        // 0..1

  int quality;         // Quality
  bool mono;           // Mono buffer (twice as long), left input only.
  bool synth;          // Wavetable synth mode instead of audio processing.
  bool freeze;         // Latched freeze button.
  bool seed_button;    // Momentary seed button (tap/hold, long press locks).
  uint8_t gain;        // 0 = automatic gain control, 1..255 = manual.

  void SetDefaults();
};

struct Status {
  bool seed_locked;
  bool seed_led;         // grain/tap activity, as the Beads seed LED
  bool frozen;
  bool input_present;
  bool seed_input_patched;
  float overwrite_reserve;
  uint8_t vu_meter;
};

class Engine {
 public:
  static const int kSampleRate = 48000;
  // Beads processes 12 frames at a time at 48 kHz (4 kHz block rate).
  static const size_t kBlockSize = 12;
  // Latency added by the internal block FIFO, in frames.
  static const size_t kLatency = kBlockSize;

  Engine()
      : delay_buffer_(NULL),
        reverb_buffer_(NULL),
        processor_(NULL),
        synth_(NULL) { }
  ~Engine();

  // Allocates the delay buffers. Call once, not from an audio thread.
  bool Init();

  // Renders `size` frames. Gate arrays hold one byte per frame (non-zero =
  // gate high) and may be NULL when the corresponding jack is not available.
  // gate_out may be NULL.
  void Process(
      const Controls& controls,
      const float* in_l,
      const float* in_r,
      const uint8_t* freeze_gate,
      const uint8_t* seed_gate,
      float* out_l,
      float* out_r,
      uint8_t* gate_out,
      size_t size);

  const Status& status() const { return status_; }

 private:
  void RenderBlock();
  void UpdateSeedButton(bool pressed);
  void UpdateConfiguration();
  void ApplyConfiguration();
  void DetectInput(const beads::FloatFrame* in, size_t size);
  void BuildParameters(beads::Parameters* p, beads::ModulatedParameter* d,
                       beads::Processor::MixParameters* m) const;
  void ProcessAudio(
      beads::SeedButtonState seed_state,
      const stmlib::GateFlags* seed,
      const bool* freeze,
      const beads::ModulatedParameter& d,
      const beads::Parameters& p,
      const beads::Processor::MixParameters& m,
      beads::FloatFrame* in,
      beads::FloatFrame* out,
      bool* gate);

  // Large buffers, allocated in Init().
  uint8_t* delay_buffer_;
  uint16_t* reverb_buffer_;

  beads::Processor* processor_;
  beads::Synth* synth_;
  GainControl gain_control_;

  // 48 kHz <-> 32 kHz for the vintage digital mode.
  RationalResampler<2, 3, 192, kBlockSize> down_;
  RationalResampler<3, 2, 192, 2 * kBlockSize / 3> up_;

  Controls controls_;
  Status status_;

  // Block FIFO.
  size_t fifo_index_;
  float in_fifo_l_[kBlockSize];
  float in_fifo_r_[kBlockSize];
  bool freeze_fifo_[kBlockSize];
  bool seed_fifo_[kBlockSize];
  float out_fifo_l_[kBlockSize];
  float out_fifo_r_[kBlockSize];
  bool gate_fifo_[kBlockSize];

  // Configuration state (quality, mono, synth) with a short fade on change.
  bool synth_mode_;
  int target_quality_;
  bool target_mono_;
  bool target_synth_;
  float fade_;
  bool reconfigure_pending_;
  bool reset_gain_pending_;

  // Seed button state machine (as beads/ui.cc).
  bool seed_locked_;
  bool seed_pressed_;
  bool seed_long_press_handled_;
  size_t seed_press_duration_;

  // Seed gate: patch detection by activity, 2.5 ms delay, gate flags.
  bool seed_gate_previous_;
  size_t seed_idle_time_;
  bool seed_delay_line_[128];
  size_t seed_delay_write_;
  stmlib::GateFlags seed_flags_previous_;
  stmlib::GateFlags seed_flags_previous_32k_;

  // Input presence detection, replacing the Beads normalisation probe.
  float input_envelope_;
  size_t input_silence_time_;

  // Grain gate LED stretcher.
  size_t seed_led_time_;

  DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace pearls

#endif  // PEARLS_ENGINE_PEARLS_ENGINE_H_
