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
// Timing of gate jacks read or driven outside the audio thread.
//
// Csound renders audio in bursts (a whole hardware period at a time), so a
// gate read once per control cycle lands up to a period early or late. A
// polling thread instead timestamps each edge, and this class converts
// timestamps to frame positions with a constant latency.
//
// For each rendered block, the "lateness" is the wall-clock time of the
// render minus the block's position in frames, in seconds. Its minimum over
// the last one to two seconds tells how early a frame can be rendered: an
// input edge is placed at the earliest frame guaranteed to be rendered after
// the edge was seen. Its maximum tells how late a frame can be rendered: an
// output edge is emitted at the latest time its frame can be rendered.
// Both follow drifts between the clocks and the timing change after an
// audio dropout within two seconds.

#ifndef PEARLS_ENGINE_GATE_TIMING_H_
#define PEARLS_ENGINE_GATE_TIMING_H_

#include <stdint.h>

#include <algorithm>

namespace pearls {

class RenderClock {
 public:
  RenderClock() { }

  // margin: time between a gate edge happening and being seen by the
  // polling thread, plus the length of a rendered block (s).
  void Init(double sample_rate, double margin, double window = 1.0) {
    sample_rate_ = sample_rate;
    margin_ = margin;
    window_frames_ = uint64_t(window * sample_rate);
    observations_ = 0;
    window_start_ = 0;
    current_min_ = current_max_ = 0.0;
    previous_min_ = previous_max_ = 0.0;
  }

  // Called for each rendered block, with the wall-clock time and the frame
  // position of its first frame.
  void Observe(double now, uint64_t frame) {
    const double lateness = now - double(frame) / sample_rate_;
    if (observations_ == 0 || frame >= window_start_ + window_frames_) {
      if (observations_ == 0) {
        previous_min_ = previous_max_ = lateness;
      } else {
        previous_min_ = current_min_;
        previous_max_ = current_max_;
      }
      current_min_ = current_max_ = lateness;
      window_start_ = frame;
    } else {
      current_min_ = std::min(current_min_, lateness);
      current_max_ = std::max(current_max_, lateness);
    }
    ++observations_;
  }

  bool valid() const { return observations_ > 0; }

  // Frame position for an input edge seen at wall-clock time t.
  double InputFrame(double t) const {
    const double lateness = std::min(previous_min_, current_min_);
    return (t - lateness + margin_) * sample_rate_;
  }

  // Wall-clock time at which an output edge at `frame` is emitted.
  double OutputTime(double frame) const {
    const double lateness = std::max(previous_max_, current_max_);
    return frame / sample_rate_ + lateness + margin_;
  }

 private:
  double sample_rate_;
  double margin_;
  uint64_t window_frames_;
  uint64_t observations_;
  uint64_t window_start_;
  double current_min_;
  double current_max_;
  double previous_min_;
  double previous_max_;
};

// Single producer, single consumer queue between two threads. T must be
// copyable; capacity a power of two.
template<typename T, size_t capacity>
class SpscQueue {
 public:
  SpscQueue() : head_(0), tail_(0) { }

  bool Push(const T& item) {
    const uint32_t tail = __atomic_load_n(&tail_, __ATOMIC_RELAXED);
    const uint32_t head = __atomic_load_n(&head_, __ATOMIC_ACQUIRE);
    if (tail - head >= capacity) {
      return false;
    }
    items_[tail & (capacity - 1)] = item;
    __atomic_store_n(&tail_, tail + 1, __ATOMIC_RELEASE);
    return true;
  }

  // Oldest item, or NULL when empty.
  const T* Peek() const {
    const uint32_t head = __atomic_load_n(&head_, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
    return head == tail ? NULL : &items_[head & (capacity - 1)];
  }

  void Pop() {
    const uint32_t head = __atomic_load_n(&head_, __ATOMIC_RELAXED);
    __atomic_store_n(&head_, head + 1, __ATOMIC_RELEASE);
  }

 private:
  T items_[capacity];
  uint32_t head_;
  uint32_t tail_;
};

}  // namespace pearls

#endif  // PEARLS_ENGINE_GATE_TIMING_H_
