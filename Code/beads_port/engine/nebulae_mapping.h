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
// Conversion from Nebulae v2 control channel values (as produced by the
// Nebulae Python control layer, CV already summed in) to Pearls controls.
//
//   Nebulae          Beads
//   ------------     -------------------------------------------
//   Start            TIME
//   Size             SIZE
//   Window           SHAPE
//   Density          DENSITY
//   Pitch encoder    PITCH (1 V/oct CV, 0.6 = unison, 0.2 per octave)
//   Speed encoder    REVERB (right half of the speed range)
//   Overlap          FEEDBACK
//   Blend            DRY/WET
//   Start/Size/Window/Pitch (secondary)   TIME/SIZE/SHAPE/PITCH randomness
//   Density (secondary)                   input gain (0 = automatic)
//   Overlap (secondary)                   reversed grains (Pearls addition)
//   Blend (secondary)                     stereo spread (Pearls addition)
//   Record (secondary)                    mono buffer
//   Freeze (secondary)                    sparse randomness flavour
//   Record                                tap tempo (Pearls addition)
//   Next held + Record                    pitch quantisation scale (Pearls)

#ifndef PEARLS_ENGINE_NEBULAE_MAPPING_H_
#define PEARLS_ENGINE_NEBULAE_MAPPING_H_

#include <algorithm>
#include <cmath>

#include "engine/pearls_engine.h"

namespace pearls {

struct NebulaeControls {
  float start;
  float size;
  float window;
  float density;
  float pitch;
  float speed;
  float overlap;
  float blend;

  float start_alt;
  float size_alt;
  float window_alt;
  float pitch_alt;
  float density_alt;
  float overlap_alt;
  float blend_alt;

  bool freeze;        // freeze button (latched)
  bool reset;         // reset button (momentary) = seed button
  bool source;        // source button (latched) = synth mode
  int quality;        // 0..3, stepped by Next presses
  int scale;          // Scale, stepped by Record presses while Next is held
  bool mono;          // secondary Record = mono buffer
  bool sparse;        // secondary Freeze = sparse randomness flavour
  bool record;        // record button (momentary) = tap tempo
};

inline float Clamp01(float x) {
  return std::min(std::max(x, 0.0f), 1.0f);
}

// Beads attenuverter law (beads/cv_reader.h, LAW_ATTENUVERTER), applied to
// the clockwise half of the pot only: secondary-layer knobs default to 0 on
// Nebulae, which must mean "no randomness". `sparse` selects the
// counter-clockwise flavour of Beads (occasional large random jumps).
inline float RandomAmountFromKnob(float knob, bool sparse) {
  float v = 0.5f * Clamp01(knob);
  v = (2.0f / 0.47f) * v * std::max(v - 0.03f, 0.0f);
  v = std::min(v, 1.0f);
  return sparse ? -v : v;
}

// Nebulae pitch: 0.6 is unison, 0.2 per octave, CV calibrated to 1 V/oct.
inline float SemitonesFromNebulaePitch(float pitch) {
  float semitones = (pitch - 0.6f) * 60.0f;
  return std::min(std::max(semitones, -48.0f), 48.0f);
}

// Nebulae speed: 0.5 is "stopped", 0.625 is 1x. Reverb uses the clockwise
// half, so a speed setting of 1x carried over from another instrument gives
// a gentle reverb (0.25).
inline float ReverbFromNebulaeSpeed(float speed) {
  return Clamp01((speed - 0.5f) * 2.0f);
}

// Secondary Density knob: fully counter-clockwise (the default) selects the
// automatic gain control, otherwise a manual gain from 1x to 40x.
inline uint8_t GainFromKnob(float knob) {
  const float k = Clamp01(knob);
  if (k < 0.02f) {
    return 0;
  }
  return uint8_t(1.0f + (k - 0.02f) / 0.98f * 254.0f);
}

// Secondary knobs for the Pearls additions: 0..1 with a small dead zone at
// the counter-clockwise end, so that the default (0) is exactly "off".
inline float AmountFromKnob(float knob) {
  const float k = Clamp01(knob);
  return k < 0.02f ? 0.0f : (k - 0.02f) / 0.98f;
}

inline void MapNebulaeControls(const NebulaeControls& n, Controls* c) {
  c->time = Clamp01(n.start);
  c->size = Clamp01(n.size);
  c->shape = Clamp01(n.window);
  c->density = Clamp01(n.density);
  c->pitch = SemitonesFromNebulaePitch(n.pitch);

  c->time_random = RandomAmountFromKnob(n.start_alt, n.sparse);
  c->size_random = RandomAmountFromKnob(n.size_alt, n.sparse);
  c->shape_random = RandomAmountFromKnob(n.window_alt, n.sparse);
  c->pitch_random = RandomAmountFromKnob(n.pitch_alt, n.sparse);

  c->feedback = Clamp01(n.overlap);
  c->dry_wet = Clamp01(n.blend);
  c->reverb = ReverbFromNebulaeSpeed(n.speed);
  c->reverse = AmountFromKnob(n.overlap_alt);
  c->spread = AmountFromKnob(n.blend_alt);

  c->quality = std::min(std::max(n.quality, 0), int(QUALITY_LAST) - 1);
  c->mono = n.mono;
  c->synth = n.source;
  c->freeze = n.freeze;
  c->seed_button = n.reset;
  c->tap = n.record;
  c->scale = std::min(std::max(n.scale, 0), int(SCALE_LAST) - 1);
  c->gain = GainFromKnob(n.density_alt);
}

}  // namespace pearls

#endif  // PEARLS_ENGINE_NEBULAE_MAPPING_H_
