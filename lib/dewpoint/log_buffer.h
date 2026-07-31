// Fixed-capacity ring buffer of recent log messages.
//
// Pure and unit-testable on the host; the Arduino wrapper that formats messages,
// mirrors them to Serial and pushes them here lives in `src/hardware/logging.*`.
//
// Zero heap allocation: lines are stored in a fixed array of char buffers.
// When full, pushing drops the oldest entry in O(1).
#pragma once

#include <cstddef>
#include <cstring>
#include <string>

namespace dpf {

// Maximum byte length of a single log line (excluding null terminator).
constexpr size_t kMaxLogLine = 256;

// Hard upper bound on capacity.  The constructor accepts any value <= this.
constexpr size_t kMaxCapacity = 50;

// Ring buffer of the most recent log lines. When full, pushing drops the oldest
// entry. Not thread-safe: callers serialise access (the device wrapper holds a
// mutex). Default capacity mirrors what fits on the /logs endpoint.
class LogBuffer {
 public:
  explicit LogBuffer(size_t capacity = kMaxCapacity);

  void push(const char* line);
  void push(const std::string& line);

  size_t size() const { return count_; }
  size_t capacity() const { return max_; }

  // Oldest-first access.  Returns nullptr if i >= size().
  const char* lineAt(size_t i) const;

 private:
  size_t max_;
  size_t head_ = 0;   // next write slot
  size_t count_ = 0;  // number of valid entries
  char data_[kMaxCapacity][kMaxLogLine];
};

}  // namespace dpf
