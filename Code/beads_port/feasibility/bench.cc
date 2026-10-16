// Worst-case CPU benchmark for the Beads DSP core (off-target).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
using namespace beads;

uint8_t block_1[512 * 1024];
uint8_t block_2[256 * 1024];
uint16_t reverb_buffer[32768];
uint8_t* buffers[3];

static void SetParams(Parameters* p, float time, float size, float shape, float pitch, float rnd) {
  p->time = {time, rnd, 0.0f, false};
  p->size = {size, rnd, 0.0f, false};
  p->shape = {shape, rnd, 0.0f, false};
  p->pitch = {pitch, rnd, 0.0f, false};
  p->monophonic_mode = false;
}

double g_checksum;
double Run(Processor::Media media, bool mono, bool synth_mode, float size_knob, size_t block, double seconds) {
  srand(1); g_checksum = 0.0;
  static Processor proc;
  static Synth synth;
  proc.Init(buffers, reverb_buffer);
  proc.Configure(media, mono);
  synth.Init(reverb_buffer);
  float sr = synth_mode ? 48000.0f : proc.sample_rate();
  Parameters p; SetParams(&p, 0.3f, size_knob, 0.5f, 0.6f, 0.3f);
  Processor::MixParameters m = {0.5f, 0.8f, 0.6f, false};
  ModulatedParameter d = {0.95f, 0.0f};  // near-max density -> many overlapping grains
  size_t total = size_t(sr * seconds);
  FloatFrame in[256], out[256]; bool freeze[256] = {false};
  double phase = 0.0;
  auto t0 = std::chrono::steady_clock::now();
  for (size_t n = 0; n < total; n += block) {
    for (size_t i = 0; i < block; ++i) {
      phase += 220.0 / sr; if (phase > 1.0) phase -= 1.0;
      float s = 0.5f * sinf(2 * M_PI * phase) + 0.05f * (rand() / float(RAND_MAX) - 0.5f);
      in[i].l = s; in[i].r = -s;
    }
    if (synth_mode)
      synth.Render(SEED_BUTTON_STATE_LOCKED, NULL, freeze, Synth::RESOLUTION_16_BIT, d, p, m, out, block);
    else
      proc.Process(SEED_BUTTON_STATE_LOCKED, NULL, freeze, d, p, m, in, out, block);
    for (size_t i = 0; i < block; ++i) g_checksum += fabs(out[i].l) + fabs(out[i].r);
  }
  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return elapsed / seconds;  // fraction of one core needed for real time
}

int main(int argc, char** argv) {
  buffers[0] = block_1; buffers[1] = block_1 + 256 * 1024; buffers[2] = block_2;
  if (argc > 1) {
    for (int media = 0; media < 4; ++media) for (int sz = 0; sz < 2; ++sz) {
      Run(Processor::Media(media), false, false, sz ? 1.0f : 0.6f, 24, 3.0);
      printf("media=%d %s mean|out|=%.6f\n", media, sz ? "delay" : "granular", g_checksum / (3.0 * 48000));
    }
    Run(Processor::MEDIA_DIGITAL, false, true, 0.6f, 24, 3.0);
    printf("synth mean|out|=%.6f\n", g_checksum / (3.0 * 48000));
    return 0;
  }
  const char* names[] = {"dirty_tape(24k)", "clean_tape(48k,2x SRC)", "vintage(32k,12bit)", "digital(48k,16bit)"};
  for (size_t block : {24, 64, 128}) {
    for (int media = 0; media < 4; ++media) {
      for (int mono = 0; mono < 2; ++mono) {
        double g = Run(Processor::Media(media), mono, false, 0.6f, block, 20.0);
        double dl = Run(Processor::Media(media), mono, false, 1.0f, block, 20.0);
        printf("block=%3zu %-24s %-6s granular=%5.2f%%  delay=%5.2f%%\n", block, names[media], mono ? "mono" : "stereo", g * 100, dl * 100);
      }
    }
    printf("block=%3zu synth(48k)                       %5.2f%%\n", block, Run(Processor::MEDIA_DIGITAL, false, true, 0.6f, block, 20.0) * 100);
  }
}
