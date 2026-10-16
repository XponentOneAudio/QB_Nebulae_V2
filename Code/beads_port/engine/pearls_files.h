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
// Files used by Pearls on the Nebulae: WAV samples, the saved state, and the
// USB stick. Plain POSIX, no threads: the Csound opcode calls these from its
// worker thread, the tests from the desktop.
//
// Locations (all under $PEARLS_ROOT when that variable is set, for tests):
//   /home/alarm/pearls   saved state (state.txt, buffer.wav)
//   /home/alarm/audio    audio files copied from the USB stick at boot
//   /mnt/memory          USB stick (/dev/sda1), mounted when needed

#ifndef PEARLS_ENGINE_PEARLS_FILES_H_
#define PEARLS_ENGINE_PEARLS_FILES_H_

#include <cstddef>
#include <string>
#include <vector>

namespace pearls {

struct Paths {
  std::string state_dir;
  std::string audio_dir;
  std::string usb_dir;
  std::string usb_device;
  // False under $PEARLS_ROOT: usb_dir is a plain directory, never mounted.
  bool mount_usb;

  static Paths Default();
  std::string state_file() const { return state_dir + "/state.txt"; }
  std::string buffer_file() const { return state_dir + "/buffer.wav"; }
};

struct AudioData {
  std::vector<float> l;
  std::vector<float> r;
  float sample_rate;
  size_t frames() const { return l.size(); }
};

// Reads a WAV file: PCM 8, 16, 24 or 32-bit, or float 32 or 64-bit, any
// number of channels (mono is copied to both sides, only the first two
// channels of multichannel files are used). Reads at most max_frames frames.
bool ReadWavFile(const std::string& path, size_t max_frames, AudioData* out,
                 std::string* error);

// Writes a 16-bit WAV file (1 channel when mono, from `l`), safely: to a
// temporary file first, synced to disk, then renamed.
bool WriteWavFile(const std::string& path, const float* l, const float* r,
                  size_t frames, int sample_rate, bool mono,
                  std::string* error);

// WAV files in a directory, sorted by name, ignoring hidden files (such as
// the "._" files macOS leaves on USB sticks).
std::vector<std::string> ListWavFiles(const std::string& dir);

// First "<prefix>NNN.wav" (NNN from 001) that doesn't exist in dir.
std::string NextFreeFileName(const std::string& dir, const std::string& prefix);

struct SavedState {
  int quality;        // -1 = none saved
  bool frozen;        // buffer.wav holds a frozen buffer to restore
  int load_index;     // last sample loaded (index in the sorted list)

  SavedState() : quality(-1), frozen(false), load_index(-1) { }
};

bool ReadSavedState(const std::string& path, SavedState* state);
bool WriteSavedState(const std::string& path, const SavedState& state);

// File name without the directory.
std::string BaseName(const std::string& path);

// Makes a directory and its parents.
bool MakeDirectories(const std::string& dir);

// Mounts the USB stick if it isn't mounted. *mounted_here tells whether to
// unmount it afterwards (UnmountUsb).
bool MountUsb(const Paths& paths, bool* mounted_here, std::string* error);
void UnmountUsb(const Paths& paths);

}  // namespace pearls

#endif  // PEARLS_ENGINE_PEARLS_FILES_H_
