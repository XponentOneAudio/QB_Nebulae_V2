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
// Files used by Pearls on the Nebulae. Only system calls that the 2017-2019
// Nebulae system images provide with the same symbol versions are used (no
// stat(), for instance: see tools/build_prebuilt.sh).

#include "engine/pearls_files.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/stat.h>  // mkdir() only
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

// libstdc++ headers built against glibc 2.32 or later read this glibc
// variable when copying strings; the Nebulae's older glibc doesn't have it.
// This private copy always says "multi-threaded", which keeps the string
// reference counts atomic, as they must be anyway.
#if defined(__has_include)
#if __has_include(<sys/single_threaded.h>)
extern "C" {
__attribute__((visibility("hidden"))) char __libc_single_threaded = 0;
}
#endif
#endif

namespace pearls {

std::string BaseName(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos
      ? path
      : std::string(path.c_str() + slash + 1);
}

namespace {

uint16_t Le16(const uint8_t* p) {
  return uint16_t(p[0] | (p[1] << 8));
}

uint32_t Le32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
      (uint32_t(p[3]) << 24);
}

void PutLe16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
}

void PutLe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v);
  p[1] = uint8_t(v >> 8);
  p[2] = uint8_t(v >> 16);
  p[3] = uint8_t(v >> 24);
}

void SetError(std::string* error, const std::string& message) {
  if (error) {
    *error = message;
  }
}

std::string SystemError(const std::string& what) {
  return what + ": " + strerror(errno);
}

bool SyncDirectory(const std::string& dir) {
  int fd = open(dir.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }
  fsync(fd);
  close(fd);
  return true;
}

std::string Parent(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos
      ? std::string(".")
      : std::string(path.c_str(), slash ? slash : 1);
}

int16_t ToPcm16(float x) {
  float v = x * 32768.0f;
  v = v >= 0.0f ? v + 0.5f : v - 0.5f;
  if (v > 32767.0f) {
    return 32767;
  } else if (v < -32768.0f) {
    return -32768;
  }
  return int16_t(v);
}

// Writes the whole file, then syncs it.
bool WriteAll(const std::string& path, const std::vector<uint8_t>& header,
              const std::vector<uint8_t>& data, std::string* error) {
  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) {
    SetError(error, SystemError("cannot create " + path));
    return false;
  }
  bool ok = fwrite(&header[0], 1, header.size(), fp) == header.size();
  if (ok && !data.empty()) {
    ok = fwrite(&data[0], 1, data.size(), fp) == data.size();
  }
  ok = fflush(fp) == 0 && ok;
  ok = fsync(fileno(fp)) == 0 && ok;
  if (fclose(fp) != 0) {
    ok = false;
  }
  if (!ok) {
    SetError(error, SystemError("cannot write " + path));
    unlink(path.c_str());
  }
  return ok;
}

bool ReplaceFile(const std::string& temp, const std::string& path,
                 std::string* error) {
  if (rename(temp.c_str(), path.c_str()) != 0) {
    SetError(error, SystemError("cannot rename " + temp));
    unlink(temp.c_str());
    return false;
  }
  SyncDirectory(Parent(path));
  return true;
}

}  // namespace

Paths Paths::Default() {
  Paths p;
  const char* root = getenv("PEARLS_ROOT");
  const std::string prefix = root ? root : "";
  p.state_dir = prefix + "/home/alarm/pearls";
  p.audio_dir = prefix + "/home/alarm/audio";
  p.usb_dir = prefix + "/mnt/memory";
  p.usb_device = "/dev/sda1";
  p.mount_usb = !root;
  return p;
}

bool ReadWavFile(const std::string& path, size_t max_frames, AudioData* out,
                 std::string* error) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) {
    SetError(error, SystemError("cannot open " + path));
    return false;
  }
  uint8_t riff[12];
  if (fread(riff, 1, 12, fp) != 12 || memcmp(riff, "RIFF", 4) ||
      memcmp(riff + 8, "WAVE", 4)) {
    fclose(fp);
    SetError(error, path + " is not a WAV file");
    return false;
  }
  uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
  uint32_t rate = 0;
  bool have_format = false;
  std::vector<uint8_t> data;
  uint8_t chunk[8];
  while (fread(chunk, 1, 8, fp) == 8) {
    const uint32_t size = Le32(chunk + 4);
    if (!memcmp(chunk, "fmt ", 4) && size >= 16 && size < 1024) {
      uint8_t fmt[1024];
      if (fread(fmt, 1, size, fp) != size) {
        break;
      }
      format = Le16(fmt);
      channels = Le16(fmt + 2);
      rate = Le32(fmt + 4);
      block_align = Le16(fmt + 12);
      bits = Le16(fmt + 14);
      if (format == 0xfffe && size >= 26) {
        format = Le16(fmt + 24);  // WAVE_FORMAT_EXTENSIBLE subformat
      }
      have_format = true;
      if (size & 1) {
        fseek(fp, 1, SEEK_CUR);
      }
    } else if (!memcmp(chunk, "data", 4)) {
      if (!have_format || !channels || !block_align) {
        break;
      }
      // Some writers leave the size at 0 or 0xffffffff: read what's there.
      size_t wanted = size_t(block_align) * max_frames;
      if (size != 0 && size != 0xffffffff) {
        wanted = std::min(wanted, size_t(size));
      }
      data.resize(wanted);
      data.resize(fread(&data[0], 1, wanted, fp));
      break;
    } else {
      if (fseek(fp, long(size) + long(size & 1), SEEK_CUR) != 0) {
        break;
      }
    }
  }
  fclose(fp);

  if (!have_format) {
    SetError(error, path + ": no format chunk");
    return false;
  }
  const bool pcm = format == 1 &&
      (bits == 8 || bits == 16 || bits == 24 || bits == 32);
  const bool ieee = format == 3 && (bits == 32 || bits == 64);
  if (!pcm && !ieee) {
    SetError(error, path + ": unsupported encoding (format " +
             std::to_string(format) + ", " + std::to_string(bits) + " bits)");
    return false;
  }
  const size_t bytes = bits / 8;
  if (!channels || block_align < bytes * channels || rate < 1000 ||
      rate > 768000) {
    SetError(error, path + ": invalid format chunk");
    return false;
  }
  const size_t frames = std::min(data.size() / block_align, max_frames);
  if (!frames) {
    SetError(error, path + ": no audio data");
    return false;
  }
  out->sample_rate = float(rate);
  out->l.resize(frames);
  out->r.resize(frames);
  for (size_t f = 0; f < frames; ++f) {
    float s[2] = { 0.0f, 0.0f };
    const size_t used = std::min(size_t(channels), size_t(2));
    for (size_t c = 0; c < used; ++c) {
      const uint8_t* p = &data[f * block_align + c * bytes];
      if (ieee && bits == 32) {
        float v;
        memcpy(&v, p, 4);
        s[c] = v;
      } else if (ieee) {
        double v;
        memcpy(&v, p, 8);
        s[c] = float(v);
      } else if (bits == 8) {
        s[c] = (float(p[0]) - 128.0f) / 128.0f;
      } else if (bits == 16) {
        s[c] = float(int16_t(Le16(p))) / 32768.0f;
      } else if (bits == 24) {
        const int32_t v = int32_t(
            (uint32_t(p[0]) << 8) | (uint32_t(p[1]) << 16) |
            (uint32_t(p[2]) << 24)) >> 8;
        s[c] = float(v) / 8388608.0f;
      } else {
        s[c] = float(int32_t(Le32(p))) / 2147483648.0f;
      }
      // NaN and out of range values from float files.
      if (!(s[c] > -1.0f)) {
        s[c] = s[c] < 0.0f ? -1.0f : 0.0f;
      } else if (s[c] > 1.0f) {
        s[c] = 1.0f;
      }
    }
    out->l[f] = s[0];
    out->r[f] = used > 1 ? s[1] : s[0];
  }
  return true;
}

bool WriteWavFile(const std::string& path, const float* l, const float* r,
                  size_t frames, int sample_rate, bool mono,
                  std::string* error) {
  const uint16_t channels = mono ? 1 : 2;
  const uint32_t data_size = uint32_t(frames * channels * 2);
  std::vector<uint8_t> header(44);
  uint8_t* h = &header[0];
  memcpy(h, "RIFF", 4);
  PutLe32(h + 4, 36 + data_size);
  memcpy(h + 8, "WAVEfmt ", 8);
  PutLe32(h + 16, 16);
  PutLe16(h + 20, 1);
  PutLe16(h + 22, channels);
  PutLe32(h + 24, uint32_t(sample_rate));
  PutLe32(h + 28, uint32_t(sample_rate) * channels * 2);
  PutLe16(h + 32, channels * 2);
  PutLe16(h + 34, 16);
  memcpy(h + 36, "data", 4);
  PutLe32(h + 40, data_size);

  std::vector<uint8_t> data(data_size);
  uint8_t* d = data.empty() ? NULL : &data[0];
  for (size_t f = 0; f < frames; ++f) {
    PutLe16(d, uint16_t(ToPcm16(l[f])));
    d += 2;
    if (!mono) {
      PutLe16(d, uint16_t(ToPcm16(r[f])));
      d += 2;
    }
  }
  const std::string temp = path + ".tmp";
  return WriteAll(temp, header, data, error) &&
      ReplaceFile(temp, path, error);
}

std::vector<std::string> ListWavFiles(const std::string& dir) {
  std::vector<std::string> files;
  DIR* d = opendir(dir.c_str());
  if (!d) {
    return files;
  }
  while (struct dirent* e = readdir(d)) {
    const std::string name = e->d_name;
    if (name.empty() || name[0] == '.' || name.size() < 5) {
      continue;
    }
    if (strcasecmp(name.c_str() + name.size() - 4, ".wav") == 0) {
      files.push_back(dir + "/" + name);
    }
  }
  closedir(d);
  std::sort(files.begin(), files.end());
  return files;
}

std::string NextFreeFileName(const std::string& dir,
                             const std::string& prefix) {
  for (int i = 1; i < 1000; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "%03d.wav", i);
    const std::string path = dir + "/" + prefix + name;
    if (access(path.c_str(), F_OK) != 0) {
      return path;
    }
  }
  return dir + "/" + prefix + "999.wav";
}

// "key value" line. (No sscanf/strtol: recent C libraries compile them to
// symbols the Nebulae's C library doesn't have.)
bool ParseLine(const char* line, std::string* key, int* value) {
  const char* p = line;
  while (*p && *p != ' ' && *p != '\n') {
    ++p;
  }
  *key = std::string(line, p);
  while (*p == ' ') {
    ++p;
  }
  bool negative = false;
  if (*p == '-') {
    negative = true;
    ++p;
  }
  if (*p < '0' || *p > '9') {
    return false;
  }
  int v = 0;
  for (; *p >= '0' && *p <= '9' && v < 100000; ++p) {
    v = v * 10 + (*p - '0');
  }
  *value = negative ? -v : v;
  return !key->empty();
}

bool ReadSavedState(const std::string& path, SavedState* state) {
  FILE* fp = fopen(path.c_str(), "r");
  if (!fp) {
    return false;
  }
  char line[128];
  while (fgets(line, sizeof(line), fp)) {
    std::string key;
    int value;
    if (!ParseLine(line, &key, &value)) {
      continue;
    }
    if (key == "quality" && value >= 0 && value <= 3) {
      state->quality = value;
    } else if (key == "frozen") {
      state->frozen = value != 0;
    } else if (key == "scale" && value >= 0 && value < 64) {
      state->scale = value;
    } else if (key == "load_index" && value >= -1) {
      state->load_index = value;
    }
  }
  fclose(fp);
  return true;
}

bool WriteSavedState(const std::string& path, const SavedState& state) {
  char text[128];
  const int n = snprintf(text, sizeof(text),
      "quality %d\nfrozen %d\nload_index %d\nscale %d\n",
      state.quality, state.frozen ? 1 : 0, state.load_index, state.scale);
  std::vector<uint8_t> header(text, text + n);
  const std::string temp = path + ".tmp";
  return WriteAll(temp, header, std::vector<uint8_t>(), NULL) &&
      ReplaceFile(temp, path, NULL);
}

bool MakeDirectories(const std::string& dir) {
  if (dir.empty()) {
    return false;
  }
  for (size_t i = 1; i <= dir.size(); ++i) {
    if (i == dir.size() || dir[i] == '/') {
      const std::string part(dir.c_str(), i);
      if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
      }
    }
  }
  return true;
}

namespace {

bool IsMounted(const std::string& dir) {
  FILE* fp = fopen("/proc/mounts", "r");
  if (!fp) {
    return false;
  }
  char line[512];
  bool found = false;
  const std::string needle = " " + dir + " ";
  while (!found && fgets(line, sizeof(line), fp)) {
    found = strstr(line, needle.c_str()) != NULL;
  }
  fclose(fp);
  return found;
}

}  // namespace

bool MountUsb(const Paths& paths, bool* mounted_here, std::string* error) {
  *mounted_here = false;
  if (!paths.mount_usb) {
    if (access(paths.usb_dir.c_str(), W_OK) == 0) {
      return true;
    }
    SetError(error, "no USB stick (" + paths.usb_dir + ")");
    return false;
  }
  if (IsMounted(paths.usb_dir)) {
    return true;
  }
  if (access(paths.usb_device.c_str(), F_OK) != 0) {
    SetError(error, "no USB stick (" + paths.usb_device + ")");
    return false;
  }
  MakeDirectories(paths.usb_dir);
  const char* types[] = {
    "vfat", "exfat", "ext4", "ext3", "ext2", "ntfs", "hfsplus", NULL
  };
  for (const char** type = types; *type; ++type) {
    if (mount(paths.usb_device.c_str(), paths.usb_dir.c_str(), *type, 0,
              NULL) == 0) {
      *mounted_here = true;
      return true;
    }
  }
  SetError(error, SystemError("cannot mount " + paths.usb_device));
  return false;
}

void UnmountUsb(const Paths& paths) {
  sync();
  if (paths.mount_usb) {
    umount2(paths.usb_dir.c_str(), 0);
  }
}

}  // namespace pearls
