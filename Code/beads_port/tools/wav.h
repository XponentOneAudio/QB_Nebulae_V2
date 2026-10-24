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
// Minimal WAV reader/writer for the desktop tools (PCM 16/24/32-bit and
// 32-bit float in, 32-bit float stereo out).

#ifndef PEARLS_TOOLS_WAV_H_
#define PEARLS_TOOLS_WAV_H_

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pearls {

struct StereoBuffer {
  std::vector<float> l;
  std::vector<float> r;
  int sample_rate;
  size_t size() const { return l.size(); }
};

inline bool ReadWav(const std::string& path, StereoBuffer* out) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) {
    return false;
  }
  char riff[12];
  if (fread(riff, 1, 12, fp) != 12 || memcmp(riff, "RIFF", 4) ||
      memcmp(riff + 8, "WAVE", 4)) {
    fclose(fp);
    return false;
  }
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  std::vector<uint8_t> data;
  char id[4];
  uint32_t chunk_size;
  while (fread(id, 1, 4, fp) == 4 && fread(&chunk_size, 4, 1, fp) == 1) {
    if (!memcmp(id, "fmt ", 4)) {
      std::vector<uint8_t> fmt(chunk_size);
      if (fread(&fmt[0], 1, chunk_size, fp) != chunk_size) break;
      memcpy(&format, &fmt[0], 2);
      memcpy(&channels, &fmt[2], 2);
      memcpy(&rate, &fmt[4], 4);
      memcpy(&bits, &fmt[14], 2);
      if (format == 0xfffe && chunk_size >= 26) {
        memcpy(&format, &fmt[24], 2);  // WAVE_FORMAT_EXTENSIBLE subformat
      }
    } else if (!memcmp(id, "data", 4)) {
      data.resize(chunk_size);
      data.resize(fread(&data[0], 1, chunk_size, fp));
    } else {
      fseek(fp, chunk_size + (chunk_size & 1), SEEK_CUR);
    }
  }
  fclose(fp);
  if (!channels || data.empty()) {
    return false;
  }
  const size_t bytes = bits / 8;
  const size_t frames = data.size() / (bytes * channels);
  out->sample_rate = int(rate);
  out->l.resize(frames);
  out->r.resize(frames);
  for (size_t f = 0; f < frames; ++f) {
    float s[2] = { 0.0f, 0.0f };
    for (size_t c = 0; c < channels && c < 2; ++c) {
      const uint8_t* p = &data[(f * channels + c) * bytes];
      if (format == 3 && bits == 32) {
        memcpy(&s[c], p, 4);
      } else if (bits == 16) {
        int16_t v;
        memcpy(&v, p, 2);
        s[c] = v / 32768.0f;
      } else if (bits == 24) {
        int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
        s[c] = (v >> 8) / 8388608.0f;
      } else if (bits == 32) {
        int32_t v;
        memcpy(&v, p, 4);
        s[c] = v / 2147483648.0f;
      }
    }
    out->l[f] = s[0];
    out->r[f] = channels > 1 ? s[1] : s[0];
  }
  return true;
}

inline bool WriteWav(const std::string& path, const StereoBuffer& in) {
  FILE* fp = fopen(path.c_str(), "wb");
  if (!fp) {
    return false;
  }
  const uint32_t frames = in.size();
  const uint32_t data_size = frames * 2 * 4;
  const uint32_t riff_size = 36 + data_size;
  const uint16_t format = 3, channels = 2, block_align = 8, bits = 32;
  const uint32_t rate = in.sample_rate;
  const uint32_t byte_rate = rate * block_align;
  const uint32_t fmt_size = 16;
  fwrite("RIFF", 1, 4, fp);
  fwrite(&riff_size, 4, 1, fp);
  fwrite("WAVEfmt ", 1, 8, fp);
  fwrite(&fmt_size, 4, 1, fp);
  fwrite(&format, 2, 1, fp);
  fwrite(&channels, 2, 1, fp);
  fwrite(&rate, 4, 1, fp);
  fwrite(&byte_rate, 4, 1, fp);
  fwrite(&block_align, 2, 1, fp);
  fwrite(&bits, 2, 1, fp);
  fwrite("data", 1, 4, fp);
  fwrite(&data_size, 4, 1, fp);
  for (uint32_t i = 0; i < frames; ++i) {
    fwrite(&in.l[i], 4, 1, fp);
    fwrite(&in.r[i], 4, 1, fp);
  }
  fclose(fp);
  return true;
}

}  // namespace pearls

#endif  // PEARLS_TOOLS_WAV_H_
