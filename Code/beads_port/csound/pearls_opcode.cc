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
// Csound plugin opcode running the Pearls (Beads) engine from Nebulae
// control channels.
//
//   aL, aR, kgate, kseedled, kreserve  pearls  ainL, ainR,
//       kstart, ksize, kwindow, kdensity, kpitch, kspeed, koverlap, kblend,
//       kstart_alt, ksize_alt, kwindow_alt, kpitch_alt, kdensity_alt,
//       kfreeze, kreset, ksource, kquality, kmono, ksparse [, igates]
//
// All k-rate inputs are Nebulae channel values (see engine/nebulae_mapping.h).
// igates = 1 reads the Freeze and Reset gate jacks directly from the GPIO
// registers (freeze gate and seed input, as on Beads) and drives the EOL
// jack with the grain gate. The instrument must then tell the Python layer
// to leave those pins alone (see Instruments/Pearls/pearls.instr).
//
// Outputs: stereo audio, kgate (grain gate during this k-cycle), kseedled
// (Beads seed LED: 1 = activity, 0.5 = locked and idle, 0 = unlocked and
// idle) and kreserve (buffer overwrite reserve while frozen, 0..1).

#include <time.h>

#include <algorithm>
#include <vector>

#include "engine/nebulae_mapping.h"
#include "engine/pearls_engine.h"
#include "engine/rpi_gpio.h"

// Last: Csound's headers define short macros (IR, IW, OK...) that clash with
// identifiers in the Beads/stmlib headers.
#include "csdl.h"

namespace {

struct PearlsState {
  pearls::Engine engine;
  pearls::NebulaeGates gates;
  bool use_gates;
  std::vector<float> in_l, in_r, out_l, out_r;
  std::vector<uint8_t> freeze_gate, seed_gate, gate_out;
  bool eol_high;
  double eol_release_time;
};

struct PEARLS {
  OPDS h;
  // Outputs.
  MYFLT* out_l;
  MYFLT* out_r;
  MYFLT* kgate;
  MYFLT* kseedled;
  MYFLT* kreserve;
  // Inputs.
  MYFLT* in_l;
  MYFLT* in_r;
  MYFLT* kstart;
  MYFLT* ksize;
  MYFLT* kwindow;
  MYFLT* kdensity;
  MYFLT* kpitch;
  MYFLT* kspeed;
  MYFLT* koverlap;
  MYFLT* kblend;
  MYFLT* kstart_alt;
  MYFLT* ksize_alt;
  MYFLT* kwindow_alt;
  MYFLT* kpitch_alt;
  MYFLT* kdensity_alt;
  MYFLT* kfreeze;
  MYFLT* kreset;
  MYFLT* ksource;
  MYFLT* kquality;
  MYFLT* kmono;
  MYFLT* ksparse;
  MYFLT* igates;
  // State.
  PearlsState* state;
};

// Minimum length of an EOL pulse, in seconds of wall-clock time. Csound
// renders audio in bursts, so gate edges must be stretched in real time to
// survive.
const double kMinEolPulse = 0.002;

double Now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
}

int pearls_deinit(CSOUND* csound, void* p_) {
  (void) csound;
  PEARLS* p = static_cast<PEARLS*>(p_);
  if (p->state) {
    if (p->state->use_gates) {
      p->state->gates.set_eol(false);
    }
    delete p->state;
    p->state = NULL;
  }
  return OK;
}

int pearls_init(CSOUND* csound, PEARLS* p) {
  if (csound->GetSr(csound) != MYFLT(pearls::Engine::kSampleRate)) {
    return csound->InitError(csound, "%s",
        "pearls: the orchestra must run at sr = 48000");
  }
  if (p->state) {
    // Re-initialisation (reinit/tie): keep the running engine.
    return OK;
  }
  PearlsState* s = new PearlsState;
  s->engine.Init();
  s->use_gates = *p->igates != MYFLT(0) && s->gates.Init();
  if (s->use_gates) {
    // The Python layer configures the EOL pin as an output without setting
    // its level: start from the idle (low gate) state.
    s->gates.set_eol(false);
  }
  if (*p->igates != MYFLT(0) && !s->use_gates) {
    csound->Warning(csound, "%s",
        "pearls: GPIO gate access unavailable (/dev/gpiomem), "
        "gate jacks are ignored");
  }
  const size_t ksmps = p->h.insdshead->ksmps;
  s->in_l.assign(ksmps, 0.0f);
  s->in_r.assign(ksmps, 0.0f);
  s->out_l.assign(ksmps, 0.0f);
  s->out_r.assign(ksmps, 0.0f);
  s->freeze_gate.assign(ksmps, 0);
  s->seed_gate.assign(ksmps, 0);
  s->gate_out.assign(ksmps, 0);
  s->eol_high = false;
  s->eol_release_time = 0.0;
  p->state = s;
  csound->RegisterDeinitCallback(csound, p, pearls_deinit);
  return OK;
}

int pearls_perf(CSOUND* csound, PEARLS* p) {
  PearlsState* s = p->state;
  if (!s) {
    // Not reached: init failed and Csound does not run perf. (PerfError is
    // avoided: its signature changed between Csound 6 releases.)
    return NOTOK;
  }
  const uint32_t offset = p->h.insdshead->ksmps_offset;
  const uint32_t early = p->h.insdshead->ksmps_no_end;
  const uint32_t ksmps = p->h.insdshead->ksmps;
  const uint32_t nsmps = ksmps - early;
  const MYFLT scale = csound->Get0dBFS(csound);
  const MYFLT inv_scale = MYFLT(1) / scale;

  pearls::NebulaeControls n;
  n.start = float(*p->kstart);
  n.size = float(*p->ksize);
  n.window = float(*p->kwindow);
  n.density = float(*p->kdensity);
  n.pitch = float(*p->kpitch);
  n.speed = float(*p->kspeed);
  n.overlap = float(*p->koverlap);
  n.blend = float(*p->kblend);
  n.start_alt = float(*p->kstart_alt);
  n.size_alt = float(*p->ksize_alt);
  n.window_alt = float(*p->kwindow_alt);
  n.pitch_alt = float(*p->kpitch_alt);
  n.density_alt = float(*p->kdensity_alt);
  n.freeze = *p->kfreeze >= MYFLT(0.5);
  n.reset = *p->kreset >= MYFLT(0.5);
  n.source = *p->ksource >= MYFLT(0.5);
  n.quality = int(*p->kquality + MYFLT(0.5));
  n.mono = *p->kmono >= MYFLT(0.5);
  n.sparse = *p->ksparse >= MYFLT(0.5);

  pearls::Controls controls;
  pearls::MapNebulaeControls(n, &controls);

  // Gate jacks: sampled once per k-cycle (Csound renders in bursts, so
  // finer polling would not improve timing).
  const bool freeze_gate = s->use_gates &&
      s->gates.gate(pearls::NebulaeGates::PIN_FREEZE);
  const bool seed_gate = s->use_gates &&
      s->gates.gate(pearls::NebulaeGates::PIN_RESET);

  for (uint32_t i = 0; i < ksmps; ++i) {
    const bool active = i >= offset && i < nsmps;
    s->in_l[i] = active ? float(p->in_l[i] * inv_scale) : 0.0f;
    s->in_r[i] = active ? float(p->in_r[i] * inv_scale) : 0.0f;
    s->freeze_gate[i] = freeze_gate;
    s->seed_gate[i] = seed_gate;
  }

  s->engine.Process(
      controls,
      &s->in_l[0], &s->in_r[0],
      &s->freeze_gate[0], &s->seed_gate[0],
      &s->out_l[0], &s->out_r[0],
      &s->gate_out[0],
      ksmps);

  bool any_gate = false;
  for (uint32_t i = 0; i < ksmps; ++i) {
    const bool active = i >= offset && i < nsmps;
    p->out_l[i] = active ? MYFLT(s->out_l[i]) * scale : MYFLT(0);
    p->out_r[i] = active ? MYFLT(s->out_r[i]) * scale : MYFLT(0);
    any_gate = any_gate || s->gate_out[i];
  }

  if (s->use_gates) {
    const double now = Now();
    if (any_gate) {
      if (!s->eol_high) {
        s->gates.set_eol(true);
        s->eol_high = true;
      }
      s->eol_release_time = now + kMinEolPulse;
    } else if (s->eol_high && now >= s->eol_release_time) {
      s->gates.set_eol(false);
      s->eol_high = false;
    }
  }

  const pearls::Status& status = s->engine.status();
  *p->kgate = any_gate ? MYFLT(1) : MYFLT(0);
  *p->kseedled = status.seed_led
      ? MYFLT(1)
      : (status.seed_locked ? MYFLT(0.5) : MYFLT(0));
  *p->kreserve = MYFLT(status.overwrite_reserve);
  return OK;
}

}  // namespace

extern "C" {

static OENTRY localops[] = {
  {
    (char*) "pearls",
    sizeof(PEARLS),
    0,
    3,
    (char*) "aakkk",
    (char*) "aakkkkkkkkkkkkkkkkkkko",
    (SUBR) pearls_init,
    (SUBR) pearls_perf,
    NULL
  }
};

// Equivalent of Csound's LINKAGE macro, written out so that one binary loads
// in every Csound 6 release: the return type of csound_opcode_init changed
// from long to int64_t over time, and returning int64_t satisfies both
// (on 32-bit ARM the low word is in r0 either way, and the high word is 0).
// The opcode only uses OPDS/INSDS fields and CSOUND functions whose layout
// is identical from Csound 6.08 to 6.18 (checked on armhf).
// csdl.h may declare csound_opcode_init with a different return type, so the
// definition gets another C++ name and the exported symbol name explicitly.
PUBLIC int64_t pearls_opcode_init(CSOUND* csound, OENTRY** ep)
    __asm__("csound_opcode_init");

PUBLIC int64_t pearls_opcode_init(CSOUND* csound, OENTRY** ep) {
  (void) csound;
  *ep = localops;
  return int64_t(sizeof(localops));
}

PUBLIC int csoundModuleInfo(void) {
  return (CS_APIVERSION << 16) + (CS_APISUBVER << 8) + int(sizeof(MYFLT));
}

}  // extern "C"
