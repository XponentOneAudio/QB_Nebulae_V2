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
//
// -----------------------------------------------------------------------------
//
// Csound plugin opcode running the Pearls (Beads) engine from Nebulae
// control channels.
//
//   aL, aR, kgate, kseedled, kreserve, kquality, kfrozen, kclock, kio,
//   kscale  pearls
//       ainL, ainR,
//       kstart, ksize, kwindow, kdensity, kpitch, kspeed, koverlap, kblend,
//       kstart_alt, ksize_alt, kwindow_alt, kpitch_alt, kdensity_alt,
//       koverlap_alt, kblend_alt,
//       kfreeze, kreset, ksource, knext, kmono, ksparse, ktap, kload, ksave
//       [, igates, ifiles]
//
// Control inputs are Nebulae channel values (see engine/nebulae_mapping.h),
// except:
//   knext   Next button level, as Beads' quality button: released after a
//           short press, it steps the quality (downwards, as Beads); after
//           a 1.5 s press, it restarts the automatic gain control. Other
//           changes (the Instr Tester's incrementing file index) each step
//           once. The opcode keeps the quality, and saves it with
//           ifiles = 1;
//   ktap    tap tempo button (Record). Pressed while Next is held, it steps
//           the pitch quantisation scale instead (kept and saved like the
//           quality), and that Next press does nothing else;
//   kload   any change loads the next WAV file from the Nebulae audio folder
//           (the USB stick's files) into the buffer, held frozen;
//   ksave   a rising edge saves the buffer to the USB stick as a WAV file.
//
// igates = 1 reads the Freeze and Reset gate jacks directly from the GPIO
// registers (freeze gate and seed input, as on Beads) and drives the EOL jack
// with the grain gate. A polling thread timestamps the gate edges, so that
// they land on the right sample despite Csound rendering audio in bursts
// (engine/gate_timing.h). The instrument must tell the Python layer to leave
// those pins alone (see pearls.instr).
//
// ifiles = 1 enables loading and saving, and remembers the quality setting
// and a frozen buffer across power cycles (engine/pearls_files.h). File work
// happens in a worker thread; the buffer itself is copied in small chunks in
// the audio thread.
//
// Outputs: stereo audio, kgate (grain gate during this k-cycle), kseedled
// (Beads seed LED: 1 = activity, 0.5 = locked and idle, 0 = unlocked and
// idle), kreserve (buffer overwrite reserve while frozen, 0..1), kquality
// (0..3), kfrozen (1 = frozen), kclock (1 = tap tempo beat, 0.5 = tap tempo
// clock running, 0 = off) and kio (0 = idle, 1 = loading or saving,
// 2 = loaded, 3 = saved, 4 = failed; results stay for 1.5 s; 5 = the
// automatic gain control is measuring the input), kscale (0..7, see
// pearls::Scale).

#include <sched.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include "engine/gate_timing.h"
#include "engine/nebulae_mapping.h"
#include "engine/pearls_engine.h"
#include "engine/pearls_files.h"
#include "engine/rpi_gpio.h"

// Last: Csound's headers define short macros (IR, IW, OK...) that clash with
// identifiers in the Beads/stmlib headers.
#include "csdl.h"

namespace {

using pearls::NebulaeGates;

double Now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return double(ts.tv_sec) + 1e-9 * double(ts.tv_nsec);
}

void SleepSeconds(double t) {
  struct timespec ts;
  ts.tv_sec = time_t(t);
  ts.tv_nsec = long((t - double(ts.tv_sec)) * 1e9);
  nanosleep(&ts, NULL);
}

template<typename T>
T Load(const T* p) {
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

template<typename T>
void Store(T* p, T v) {
  __atomic_store_n(p, v, __ATOMIC_RELEASE);
}

const double kPollInterval = 0.0002;    // s, gate polling thread
const double kMinEolPulse = 0.002;      // s
// Times below are in audio time (frames rendered), which is real time on
// the Nebulae and faster when rendering to a file.
const double kIoResultTime = 1.5;       // s, kio shows the result
// As Beads: a buffer frozen (by the button or the gate) for 10 s is saved.
const double kPersistDelay = 10.0;      // s frozen before saving the buffer
const double kGainRestartPress = 1.5;   // s, Next long press (as Beads)
const double kStateDelay = 2.0;         // s after a quality change
// Load/save requests are ignored at first: the Python layer restores the
// saved secondary button values (e.g. the Next counter) after Csound starts.
const double kSettleTime = 2.0;         // s

// Jobs passed between the audio thread and the worker thread. Each value
// says which thread owns the shared fields: the other one only reads `job`.
enum Job {
  JOB_IDLE,              // audio thread
  JOB_RESTORE,           // worker: read the saved buffer
  JOB_LOAD,              // worker: read the next sample
  JOB_LOAD_READY,        // audio: load_data is ready to go to the engine
  JOB_LOADING,           // audio: engine is loading
  JOB_SAVING,            // audio: engine is copying the buffer out
  JOB_WRITE_USB,         // worker: write save_l/save_r to the USB stick
  JOB_WRITE_PERSIST,     // worker: write save_l/save_r as the saved buffer
  JOB_WRITE_STATE,       // worker: write the state file
  JOB_DONE,              // audio: worker finished, `job_ok` has the result
};

// What the current job is for.
enum Task {
  TASK_NONE,
  TASK_RESTORE,      // restoring the saved frozen buffer at start
  TASK_LOAD,         // user: load the next sample
  TASK_SAVE,         // user: save the buffer to the USB stick
  TASK_PERSIST,      // save the frozen buffer for the next start
  TASK_STATE         // save the state file only
};

enum IoStatus {
  IO_IDLE,
  IO_BUSY,
  IO_LOADED,
  IO_SAVED,
  IO_FAILED,
  IO_GAIN_ADJUSTING
};

struct GateEdge {
  double time;
  uint8_t pin;
  uint8_t level;
};

struct EolEdge {
  double time;
  uint8_t level;
};

struct PearlsState {
  CSOUND* csound;
  pearls::Engine engine;
  std::vector<float> in_l, in_r, out_l, out_r;
  std::vector<uint8_t> freeze_gate, seed_gate, gate_out;
  int quality;
  int scale;

  // Gates.
  NebulaeGates gates;
  bool use_gates;
  pearls::RenderClock render_clock;
  uint64_t frame;
  pearls::SpscQueue<GateEdge, 256> gate_edges;  // poller -> audio
  pearls::SpscQueue<EolEdge, 1024> eol_edges;   // audio -> poller
  bool freeze_level;
  bool seed_level;
  bool eol_level;
  uint64_t eol_release_frame;
  void* poller;
  int quit_poller;

  // Files.
  bool use_files;
  pearls::Paths paths;
  void* worker;
  int quit_worker;
  int job;
  bool job_ok;
  int task;                  // Task, set by the audio thread
  pearls::AudioData load_data;
  std::vector<float> save_l, save_r;
  size_t save_frames;
  int save_rate;
  bool save_mono;
  pearls::SavedState saved;  // as on disk (worker) / to write (audio)
  pearls::SavedState wanted; // audio thread's view
  uint32_t persist_generation;
  uint32_t saving_generation;
  bool persisted_frozen;
  bool load_requested;
  bool save_requested;
  double state_change_time;
  bool state_dirty;
  double frozen_since;
  int io_status;
  double io_status_time;
  MYFLT previous_load;
  MYFLT previous_next;
  bool next_pressed;
  bool next_is_index;        // Next is the Instr Tester's file index
  bool next_combo;
  double next_press_time;
  bool tap_previous;
  bool tap_masked;
  bool previous_save;

  PearlsState()
      : csound(NULL), quality(pearls::QUALITY_DIGITAL),
        scale(pearls::SCALE_OFF),
        use_gates(false), frame(0),
        freeze_level(false), seed_level(false), eol_level(false),
        eol_release_frame(0), poller(NULL), quit_poller(0),
        use_files(false), worker(NULL), quit_worker(0), job(JOB_IDLE),
        job_ok(false), task(TASK_NONE), save_frames(0), save_rate(48000),
        save_mono(false), persist_generation(0), saving_generation(0),
        persisted_frozen(false), load_requested(false),
        save_requested(false), state_change_time(0.0), state_dirty(false),
        frozen_since(-1.0), io_status(IO_IDLE), io_status_time(0.0),
        previous_load(0), previous_next(0), next_pressed(false),
        next_is_index(false),
        next_combo(false), next_press_time(0.0), tap_previous(false),
        tap_masked(false), previous_save(false) { }
};

struct PEARLS {
  OPDS h;
  // Outputs.
  MYFLT* out_l;
  MYFLT* out_r;
  MYFLT* kgate;
  MYFLT* kseedled;
  MYFLT* kreserve;
  MYFLT* kquality;
  MYFLT* kfrozen;
  MYFLT* kclock;
  MYFLT* kio;
  MYFLT* kscale;
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
  MYFLT* koverlap_alt;
  MYFLT* kblend_alt;
  MYFLT* kfreeze;
  MYFLT* kreset;
  MYFLT* ksource;
  MYFLT* knext;
  MYFLT* kmono;
  MYFLT* ksparse;
  MYFLT* ktap;
  MYFLT* kload;
  MYFLT* ksave;
  MYFLT* igates;
  MYFLT* ifiles;
  // State.
  PearlsState* state;
};

// --- Gate polling thread -----------------------------------------------------

uintptr_t PollerThread(void* arg) {
  PearlsState* s = static_cast<PearlsState*>(arg);
  // Real-time priority if allowed (Nebulae runs as root): polling needs
  // little CPU but should not wait behind other threads.
  struct sched_param param;
  param.sched_priority = 20;
  sched_setscheduler(0, SCHED_FIFO, &param);

  const int pins[2] = { NebulaeGates::PIN_FREEZE, NebulaeGates::PIN_RESET };
  bool levels[2] = { false, false };
  while (!Load(&s->quit_poller)) {
    const double now = Now();
    const uint32_t gates = s->gates.gates();
    for (int i = 0; i < 2; ++i) {
      const bool level = (gates >> pins[i]) & 1;
      if (level != levels[i]) {
        GateEdge e;
        e.time = now;
        e.pin = uint8_t(pins[i]);
        e.level = level;
        if (s->gate_edges.Push(e)) {
          levels[i] = level;
        }
      }
    }
    while (const EolEdge* e = s->eol_edges.Peek()) {
      if (e->time > now) {
        break;
      }
      s->gates.set_eol(e->level != 0);
      s->eol_edges.Pop();
    }
    SleepSeconds(kPollInterval);
  }
  return 0;
}

// --- File worker thread ------------------------------------------------------

void Finish(PearlsState* s, bool ok) {
  s->job_ok = ok;
  Store(&s->job, int(JOB_DONE));
}

void Log(PearlsState* s, const std::string& message) {
  s->csound->Message(s->csound, "pearls: %s\n", message.c_str());
}

// Longest source needed to fill the longest buffer (32 s), at any rate.
size_t MaxSourceFrames(float rate) {
  return size_t(32.5 * rate);
}

void DoRestore(PearlsState* s) {
  std::string error;
  pearls::AudioData& d = s->load_data;
  // The rate is only known after reading the header: read up to the
  // largest supported size.
  if (pearls::ReadWavFile(s->paths.buffer_file(), MaxSourceFrames(96000.0f),
                          &d, &error)) {
    Log(s, "restoring the frozen buffer");
    Store(&s->job, int(JOB_LOAD_READY));
  } else {
    Log(s, error);
    Finish(s, false);
  }
}

void DoLoad(PearlsState* s) {
  std::vector<std::string> files = pearls::ListWavFiles(s->paths.audio_dir);
  bool mounted_here = false;
  if (files.empty()) {
    std::string error;
    if (pearls::MountUsb(s->paths, &mounted_here, &error)) {
      files = pearls::ListWavFiles(s->paths.usb_dir);
    }
  }
  bool ok = false;
  if (files.empty()) {
    Log(s, "no WAV files in " + s->paths.audio_dir + " or on the USB stick");
  } else {
    // Try the next file, skipping unreadable ones.
    for (size_t attempt = 0; attempt < files.size() && !ok; ++attempt) {
      const int index = (s->saved.load_index + 1) % int(files.size());
      s->saved.load_index = index;
      std::string error;
      ok = pearls::ReadWavFile(files[index], MaxSourceFrames(96000.0f),
                               &s->load_data, &error);
      Log(s, ok ? "loading " + files[index] : error);
    }
  }
  if (mounted_here) {
    pearls::UnmountUsb(s->paths);
  }
  if (ok) {
    Store(&s->job, int(JOB_LOAD_READY));
  } else {
    Finish(s, false);
  }
}

void DoWriteUsb(PearlsState* s) {
  std::string error;
  bool mounted_here = false;
  bool ok = false;
  std::string name;
  if (pearls::MountUsb(s->paths, &mounted_here, &error)) {
    const std::string path = pearls::NextFreeFileName(
        s->paths.usb_dir, "pearls_");
    name = pearls::BaseName(path);
    ok = pearls::WriteWavFile(path, &s->save_l[0], &s->save_r[0],
        s->save_frames, s->save_rate, s->save_mono, &error);
    if (mounted_here) {
      pearls::UnmountUsb(s->paths);
    }
  }
  Log(s, ok ? "saved " + name + " to the USB stick" : error);
  // Also into the audio folder, so it can be loaded right away (Nebulae
  // replaces this folder with the stick's audio files at boot).
  if (pearls::MakeDirectories(s->paths.audio_dir)) {
    if (name.empty()) {
      name = pearls::BaseName(
          pearls::NextFreeFileName(s->paths.audio_dir, "pearls_"));
    }
    pearls::WriteWavFile(s->paths.audio_dir + "/" + name, &s->save_l[0],
        &s->save_r[0], s->save_frames, s->save_rate, s->save_mono, NULL);
  }
  Finish(s, ok);
}

void DoWritePersist(PearlsState* s) {
  std::string error;
  bool ok = pearls::MakeDirectories(s->paths.state_dir) &&
      pearls::WriteWavFile(s->paths.buffer_file(), &s->save_l[0],
          &s->save_r[0], s->save_frames, s->save_rate, s->save_mono, &error);
  if (ok) {
    ok = pearls::WriteSavedState(s->paths.state_file(), s->saved);
  } else {
    Log(s, error);
  }
  Finish(s, ok);
}

void DoWriteState(PearlsState* s) {
  const bool ok = pearls::MakeDirectories(s->paths.state_dir) &&
      pearls::WriteSavedState(s->paths.state_file(), s->saved);
  Finish(s, ok);
}

uintptr_t WorkerThread(void* arg) {
  PearlsState* s = static_cast<PearlsState*>(arg);
  // Below the audio thread: file and USB work can wait.
  if (nice(10) == -1) {
    // Not fatal.
  }
  while (!Load(&s->quit_worker)) {
    switch (Load(&s->job)) {
      case JOB_RESTORE: DoRestore(s); break;
      case JOB_LOAD: DoLoad(s); break;
      case JOB_WRITE_USB: DoWriteUsb(s); break;
      case JOB_WRITE_PERSIST: DoWritePersist(s); break;
      case JOB_WRITE_STATE: DoWriteState(s); break;
      default: SleepSeconds(0.01); break;
    }
  }
  return 0;
}

// --- Audio thread side of the file jobs ---------------------------------------

void SetIoStatus(PearlsState* s, int status, double now) {
  s->io_status = status;
  s->io_status_time = now;
}

void StartJob(PearlsState* s, int task, int job) {
  s->task = task;
  Store(&s->job, job);
}

void EndJob(PearlsState* s) {
  s->task = TASK_NONE;
  Store(&s->job, int(JOB_IDLE));
}

void UpdateFiles(PearlsState* s, double now) {
  pearls::Engine& engine = s->engine;
  const pearls::Status& status = engine.status();

  // How long the buffer has been frozen (button, load or gate).
  if (status.frozen) {
    if (s->frozen_since < 0.0) {
      s->frozen_since = now;
    }
  } else {
    s->frozen_since = -1.0;
  }

  switch (Load(&s->job)) {
    case JOB_LOAD_READY: {
      const pearls::AudioData& d = s->load_data;
      if (engine.StartLoad(&d.l[0], &d.r[0], d.frames(), d.sample_rate,
                           true)) {
        Store(&s->job, int(JOB_LOADING));
      }
      return;
    }
    case JOB_LOADING:
      if (engine.transfer_state() == pearls::TRANSFER_DONE) {
        engine.EndTransfer();
        if (s->task == TASK_LOAD) {
          // A new sample: saved for the next start once it has stayed
          // frozen for a while (TASK_PERSIST).
          SetIoStatus(s, IO_LOADED, now);
          s->wanted.load_index = s->saved.load_index;
          s->persisted_frozen = false;
          s->state_dirty = true;
        } else {
          // Restored: identical to the saved copy.
          s->persist_generation = engine.buffer_generation();
          s->persisted_frozen = true;
        }
        EndJob(s);
      }
      return;
    case JOB_SAVING:
      if (engine.transfer_state() == pearls::TRANSFER_DONE) {
        engine.EndTransfer();
        s->save_frames = engine.saved_frames();
        s->save_rate = int(engine.saved_sample_rate() + 0.5f);
        s->save_mono = engine.saved_mono();
        if (s->save_frames == 0) {
          // Empty buffer (e.g. just after a quality change).
          if (s->task == TASK_SAVE) {
            SetIoStatus(s, IO_FAILED, now);
          } else {
            s->persist_generation = s->saving_generation;
            s->persisted_frozen = false;
            s->state_dirty = true;
          }
          EndJob(s);
        } else if (s->task == TASK_SAVE) {
          Store(&s->job, int(JOB_WRITE_USB));
        } else {
          s->saved = s->wanted;
          s->saved.frozen = true;
          Store(&s->job, int(JOB_WRITE_PERSIST));
        }
      }
      return;
    case JOB_DONE:
      switch (s->task) {
        case TASK_LOAD:  // failed (success ends in JOB_LOADING)
          s->wanted.load_index = s->saved.load_index;
          SetIoStatus(s, IO_FAILED, now);
          break;
        case TASK_SAVE:
          SetIoStatus(s, s->job_ok ? IO_SAVED : IO_FAILED, now);
          break;
        case TASK_PERSIST:
          if (s->job_ok) {
            s->persist_generation = s->saving_generation;
            s->persisted_frozen = true;
          } else {
            // Don't retry until the buffer changes.
            s->persist_generation = s->saving_generation;
          }
          break;
        case TASK_STATE:
          break;
        case TASK_RESTORE:  // failed: forget the saved buffer
          s->persisted_frozen = true;
          break;
      }
      EndJob(s);
      return;
    case JOB_IDLE:
      break;
    default:
      return;  // the worker is busy
  }

  // Idle: start the next job, user requests first.
  if (s->load_requested) {
    s->load_requested = false;
    s->saved = s->wanted;
    SetIoStatus(s, IO_BUSY, now);
    StartJob(s, TASK_LOAD, JOB_LOAD);
  } else if (s->save_requested) {
    s->save_requested = false;
    if (engine.StartSave(&s->save_l[0], &s->save_r[0], s->save_l.size())) {
      SetIoStatus(s, IO_BUSY, now);
      StartJob(s, TASK_SAVE, JOB_SAVING);
    }
  } else if (status.frozen && s->frozen_since >= 0.0 &&
             now - s->frozen_since >= kPersistDelay &&
             (engine.buffer_generation() != s->persist_generation ||
              !s->persisted_frozen)) {
    if (engine.StartSave(&s->save_l[0], &s->save_r[0], s->save_l.size())) {
      s->saving_generation = engine.buffer_generation();
      StartJob(s, TASK_PERSIST, JOB_SAVING);
    }
  } else if (!status.frozen && s->persisted_frozen) {
    // Released: don't restore this buffer at the next start.
    s->persisted_frozen = false;
    s->state_dirty = true;
    s->state_change_time = now - kStateDelay;
  } else if (s->state_dirty && now - s->state_change_time >= kStateDelay) {
    s->state_dirty = false;
    s->saved = s->wanted;
    s->saved.frozen = s->persisted_frozen;
    StartJob(s, TASK_STATE, JOB_WRITE_STATE);
  }
}

// --- Opcode ------------------------------------------------------------------

int pearls_deinit(CSOUND* csound, void* p_) {
  PEARLS* p = static_cast<PEARLS*>(p_);
  PearlsState* s = p->state;
  if (s) {
    if (s->worker) {
      // Let a file job in progress finish (e.g. the state being saved when
      // switching to another instrument), for at most 3 s.
      for (int i = 0; i < 300; ++i) {
        const int job = Load(&s->job);
        if (job != JOB_RESTORE && job != JOB_LOAD &&
            job != JOB_WRITE_USB && job != JOB_WRITE_PERSIST &&
            job != JOB_WRITE_STATE) {
          break;
        }
        SleepSeconds(0.01);
      }
    }
    if (s->poller) {
      Store(&s->quit_poller, 1);
      csound->JoinThread(s->poller);
    }
    if (s->worker) {
      Store(&s->quit_worker, 1);
      csound->JoinThread(s->worker);
    }
    if (s->use_gates) {
      s->gates.set_eol(false);
    }
    delete s;
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
  s->csound = csound;
  s->engine.Init();
  const size_t ksmps = p->h.insdshead->ksmps;
  s->in_l.assign(ksmps, 0.0f);
  s->in_r.assign(ksmps, 0.0f);
  s->out_l.assign(ksmps, 0.0f);
  s->out_r.assign(ksmps, 0.0f);
  s->freeze_gate.assign(ksmps, 0);
  s->seed_gate.assign(ksmps, 0);
  s->gate_out.assign(ksmps, 0);

  s->use_gates = *p->igates != MYFLT(0) && s->gates.Init();
  if (s->use_gates) {
    // The Python layer configures the EOL pin as an output without setting
    // its level: start from the idle (low gate) state.
    s->gates.set_eol(false);
    s->render_clock.Init(
        pearls::Engine::kSampleRate,
        2.0 * kPollInterval + double(ksmps) / pearls::Engine::kSampleRate);
    s->poller = csound->CreateThread(PollerThread, s);
    if (!s->poller) {
      s->use_gates = false;
    }
  }
  if (*p->igates != MYFLT(0) && !s->use_gates) {
    csound->Warning(csound, "%s",
        "pearls: GPIO gate access unavailable (/dev/gpiomem), "
        "gate jacks are ignored");
  }

  s->use_files = *p->ifiles != MYFLT(0);
  if (s->use_files) {
    s->paths = pearls::Paths::Default();
    pearls::ReadSavedState(s->paths.state_file(), &s->saved);
    if (s->saved.quality >= 0) {
      s->quality = s->saved.quality;
    }
    if (s->saved.scale >= 0 && s->saved.scale < pearls::SCALE_LAST) {
      s->scale = s->saved.scale;
    }
    s->wanted = s->saved;
    s->wanted.quality = s->quality;
    s->wanted.scale = s->scale;
    s->save_l.assign(pearls::Engine::MaxBufferFrames(), 0.0f);
    s->save_r.assign(pearls::Engine::MaxBufferFrames(), 0.0f);
    if (s->saved.frozen) {
      s->task = TASK_RESTORE;
      s->job = JOB_RESTORE;
    }
    s->worker = csound->CreateThread(WorkerThread, s);
    if (!s->worker) {
      s->use_files = false;
      csound->Warning(csound, "%s", "pearls: cannot start the file thread");
    }
  }

  p->state = s;
  csound->RegisterDeinitCallback(csound, p, pearls_deinit);
  return OK;
}

// Fills the gate arrays from the timestamped edges.
void ReadGates(PearlsState* s, uint32_t ksmps) {
  uint32_t cursor = 0;
  while (const GateEdge* e = s->gate_edges.Peek()) {
    const double position = s->render_clock.InputFrame(e->time) -
        double(s->frame);
    if (position >= double(ksmps)) {
      break;  // a later cycle
    }
    const uint32_t index = position > 0.0 ? uint32_t(position) : 0;
    for (; cursor < index; ++cursor) {
      s->freeze_gate[cursor] = s->freeze_level;
      s->seed_gate[cursor] = s->seed_level;
    }
    if (e->pin == NebulaeGates::PIN_FREEZE) {
      s->freeze_level = e->level != 0;
    } else {
      s->seed_level = e->level != 0;
    }
    s->gate_edges.Pop();
  }
  for (; cursor < ksmps; ++cursor) {
    s->freeze_gate[cursor] = s->freeze_level;
    s->seed_gate[cursor] = s->seed_level;
  }
}

// Schedules the EOL edges of this cycle, stretching short pulses.
void WriteEol(PearlsState* s, uint32_t ksmps) {
  const uint64_t min_pulse = uint64_t(
      kMinEolPulse * pearls::Engine::kSampleRate);
  for (uint32_t i = 0; i < ksmps; ++i) {
    const uint64_t f = s->frame + i;
    const bool g = s->gate_out[i] != 0;
    bool change = false;
    if (g && !s->eol_level) {
      s->eol_level = true;
      s->eol_release_frame = f + min_pulse;
      change = true;
    } else if (!g && s->eol_level && f >= s->eol_release_frame) {
      s->eol_level = false;
      change = true;
    }
    if (change) {
      EolEdge e;
      e.time = s->render_clock.OutputTime(double(f));
      e.level = s->eol_level;
      s->eol_edges.Push(e);
    }
  }
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
  // Audio time for the controls and files, wall-clock time for the gates.
  const double now = double(s->frame) / pearls::Engine::kSampleRate;

  // Buttons: quality steps, gain restart, scale steps, load and save
  // requests. Next acts when released, as Beads' quality button.
  const MYFLT next = *p->knext;
  int quality_steps = 0;
  if (next != s->previous_next) {
    s->next_is_index = s->next_is_index ||
        (next != MYFLT(0) && next != MYFLT(1));
    if (s->previous_next == MYFLT(0) && next == MYFLT(1) &&
        !s->next_is_index) {
      s->next_pressed = true;
      s->next_combo = false;
      s->next_press_time = now;
    } else if (next == MYFLT(0) && s->next_pressed) {
      if (s->next_combo) {
        // Next + Record: nothing else.
      } else if (now - s->next_press_time >= kGainRestartPress) {
        s->engine.RestartGainControl();
      } else {
        ++quality_steps;
      }
      s->next_pressed = false;
    } else {
      // An incrementing index (Instr Tester): every change is a press.
      quality_steps += s->next_pressed ? 2 : 1;
      s->next_pressed = false;
    }
    s->previous_next = next;
  }
  // Record: tap tempo, or the scale while Next is held. A press that steps
  // the scale is hidden from the tap tempo until it is released.
  const bool tap = *p->ktap >= MYFLT(0.5);
  if (tap && !s->tap_previous && s->next_pressed) {
    s->next_combo = true;
    s->tap_masked = true;
    s->scale = (s->scale + 1) % pearls::SCALE_LAST;
    s->wanted.scale = s->scale;
    s->state_dirty = true;
    s->state_change_time = now;
  }
  if (!tap) {
    s->tap_masked = false;
  }
  s->tap_previous = tap;
  if (quality_steps) {
    // Downwards, as the Beads quality button.
    s->quality = (s->quality + quality_steps * (pearls::QUALITY_LAST - 1)) %
        pearls::QUALITY_LAST;
    s->wanted.quality = s->quality;
    s->state_dirty = true;
    s->state_change_time = now;
  }
  const bool save = *p->ksave >= MYFLT(0.5);
  if (now >= kSettleTime) {
    if (*p->kload != s->previous_load && s->use_files) {
      s->load_requested = true;
    }
    if (save && !s->previous_save && s->use_files) {
      s->save_requested = true;
    }
  }
  s->previous_load = *p->kload;
  s->previous_save = save;

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
  n.overlap_alt = float(*p->koverlap_alt);
  n.blend_alt = float(*p->kblend_alt);
  n.freeze = *p->kfreeze >= MYFLT(0.5);
  n.reset = *p->kreset >= MYFLT(0.5);
  n.source = *p->ksource >= MYFLT(0.5);
  n.quality = s->quality;
  n.mono = *p->kmono >= MYFLT(0.5);
  n.sparse = *p->ksparse >= MYFLT(0.5);
  n.record = tap && !s->tap_masked;
  n.scale = s->scale;

  pearls::Controls controls;
  pearls::MapNebulaeControls(n, &controls);

  if (s->use_gates) {
    s->render_clock.Observe(Now(), s->frame);
    ReadGates(s, ksmps);
  }

  for (uint32_t i = 0; i < ksmps; ++i) {
    const bool active = i >= offset && i < nsmps;
    s->in_l[i] = active ? float(p->in_l[i] * inv_scale) : 0.0f;
    s->in_r[i] = active ? float(p->in_r[i] * inv_scale) : 0.0f;
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
    WriteEol(s, ksmps);
  }
  s->frame += ksmps;

  if (s->use_files) {
    UpdateFiles(s, now);
  }
  if (s->io_status >= IO_LOADED && now - s->io_status_time > kIoResultTime) {
    s->io_status = IO_IDLE;
  }

  const pearls::Status& status = s->engine.status();
  *p->kgate = any_gate ? MYFLT(1) : MYFLT(0);
  *p->kseedled = status.seed_led
      ? MYFLT(1)
      : (status.seed_locked ? MYFLT(0.5) : MYFLT(0));
  *p->kreserve = MYFLT(status.overwrite_reserve);
  *p->kquality = MYFLT(s->quality);
  *p->kfrozen = status.frozen ? MYFLT(1) : MYFLT(0);
  *p->kclock = status.clock_led
      ? MYFLT(1)
      : (status.clock_running ? MYFLT(0.5) : MYFLT(0));
  *p->kscale = MYFLT(s->scale);
  *p->kio = MYFLT(s->io_status == IO_IDLE && status.gain_adjusting
      ? int(IO_GAIN_ADJUSTING) : s->io_status);
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
    (char*) "aakkkkkkkk",
    (char*) "aakkkkkkkkkkkkkkkkkkkkkkkkoo",
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
