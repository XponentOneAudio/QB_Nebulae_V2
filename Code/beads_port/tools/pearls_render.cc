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
// Desktop renderer: processes a WAV file through Pearls with Nebulae-style
// control values, optionally changing over time.
//
//   pearls_render [-i in.wav] -o out.wav [-t seconds] [name=value[@time] ...]
//
// Names are Nebulae controls: start size window density pitch speed overlap
// blend start_alt size_alt window_alt pitch_alt density_alt freeze reset
// source quality record_alt reset_alt, plus the gate inputs freeze_gate and
// seed_gate. Without -i, a test signal (two sines) is used.
//
// Example: pearls_render -i loop.wav -o out.wav density=0.8 freeze=1@4

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/nebulae_mapping.h"
#include "engine/pearls_engine.h"
#include "tools/wav.h"

using namespace pearls;

namespace {

struct Event {
  double time;
  std::string name;
  float value;
};

bool Apply(const Event& e, NebulaeControls* n, float* freeze_gate,
           float* seed_gate) {
  std::map<std::string, float*> floats;
  floats["start"] = &n->start;
  floats["size"] = &n->size;
  floats["window"] = &n->window;
  floats["density"] = &n->density;
  floats["pitch"] = &n->pitch;
  floats["speed"] = &n->speed;
  floats["overlap"] = &n->overlap;
  floats["blend"] = &n->blend;
  floats["start_alt"] = &n->start_alt;
  floats["size_alt"] = &n->size_alt;
  floats["window_alt"] = &n->window_alt;
  floats["pitch_alt"] = &n->pitch_alt;
  floats["density_alt"] = &n->density_alt;
  floats["freeze_gate"] = freeze_gate;
  floats["seed_gate"] = seed_gate;
  if (floats.count(e.name)) {
    *floats[e.name] = e.value;
    return true;
  }
  const bool b = e.value >= 0.5f;
  if (e.name == "freeze") n->freeze = b;
  else if (e.name == "reset") n->reset = b;
  else if (e.name == "source") n->source = b;
  else if (e.name == "record_alt") n->record_alt = b;
  else if (e.name == "reset_alt") n->reset_alt = b;
  else if (e.name == "quality") n->quality = int(e.value);
  else return false;
  return true;
}

void Usage() {
  fprintf(stderr,
      "usage: pearls_render [-i in.wav] -o out.wav [-t seconds] "
      "[name=value[@time] ...]\n");
  exit(1);
}

}  // namespace

int main(int argc, char** argv) {
  std::string in_path, out_path;
  double seconds = -1.0;
  std::vector<Event> events;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "-i") && i + 1 < argc) {
      in_path = argv[++i];
    } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
      out_path = argv[++i];
    } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
      seconds = atof(argv[++i]);
    } else if (strchr(argv[i], '=')) {
      std::string arg = argv[i];
      Event e;
      size_t eq = arg.find('=');
      size_t at = arg.find('@');
      e.name = arg.substr(0, eq);
      e.value = atof(arg.substr(eq + 1, at - eq - 1).c_str());
      e.time = at == std::string::npos ? 0.0 : atof(arg.substr(at + 1).c_str());
      events.push_back(e);
    } else {
      Usage();
    }
  }
  if (out_path.empty()) {
    Usage();
  }

  StereoBuffer input;
  if (!in_path.empty()) {
    if (!ReadWav(in_path, &input)) {
      fprintf(stderr, "cannot read %s\n", in_path.c_str());
      return 1;
    }
    if (input.sample_rate != Engine::kSampleRate) {
      fprintf(stderr, "warning: %s is %d Hz, processed as 48 kHz\n",
              in_path.c_str(), input.sample_rate);
    }
  }
  if (seconds < 0.0) {
    seconds = in_path.empty() ? 10.0 : double(input.size()) / 48000.0;
  }
  const size_t total = size_t(seconds * 48000.0);

  // Nebulae defaults (config/defaultnebsettings.txt, Pearls instrument).
  NebulaeControls n;
  memset(&n, 0, sizeof(n));
  n.start = 0.3f;
  n.size = 0.5f;
  n.window = 0.5f;
  n.density = 0.75f;
  n.pitch = 0.6f;
  n.speed = 0.625f;
  n.overlap = 0.0f;
  n.blend = 0.5f;
  n.quality = QUALITY_DIGITAL;
  float freeze_gate = 0.0f, seed_gate = 0.0f;

  Engine engine;
  engine.Init();
  StereoBuffer out;
  out.sample_rate = Engine::kSampleRate;
  out.l.resize(total);
  out.r.resize(total);

  const size_t kBlock = 64;
  float in_l[kBlock], in_r[kBlock];
  uint8_t fg[kBlock], sg[kBlock], gate[kBlock];
  size_t gates = 0;
  bool previous_gate = false;
  for (size_t pos = 0; pos < total; pos += kBlock) {
    const size_t size = std::min(kBlock, total - pos);
    const double now = double(pos) / 48000.0;
    for (size_t e = 0; e < events.size(); ++e) {
      if (events[e].time <= now && events[e].time > now - kBlock / 48000.0) {
        if (!Apply(events[e], &n, &freeze_gate, &seed_gate)) {
          fprintf(stderr, "unknown control: %s\n", events[e].name.c_str());
          return 1;
        }
      }
    }
    for (size_t i = 0; i < size; ++i) {
      const size_t f = pos + i;
      if (!in_path.empty()) {
        in_l[i] = f < input.size() ? input.l[f] : 0.0f;
        in_r[i] = f < input.size() ? input.r[f] : 0.0f;
      } else {
        const float t = float(f) / 48000.0f;
        in_l[i] = 0.4f * sinf(2.0f * float(M_PI) * 220.0f * t);
        in_r[i] = 0.4f * sinf(2.0f * float(M_PI) * 330.0f * t);
      }
      fg[i] = freeze_gate >= 0.5f;
      sg[i] = seed_gate >= 0.5f;
    }
    Controls c;
    MapNebulaeControls(n, &c);
    engine.Process(c, in_l, in_r, fg, sg, &out.l[pos], &out.r[pos], gate, size);
    for (size_t i = 0; i < size; ++i) {
      gates += gate[i] && !previous_gate;
      previous_gate = gate[i];
    }
  }
  if (!WriteWav(out_path, out)) {
    fprintf(stderr, "cannot write %s\n", out_path.c_str());
    return 1;
  }
  printf("wrote %s (%.1f s, %zu grain/tap gates)\n", out_path.c_str(),
         seconds, gates);
  return 0;
}
