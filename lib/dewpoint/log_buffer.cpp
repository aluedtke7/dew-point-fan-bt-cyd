#include "log_buffer.h"

#include <algorithm>
#include <cstring>

namespace dpf {

LogBuffer::LogBuffer(size_t capacity)
    : max_(capacity < 1 ? 1 : (capacity > kMaxCapacity ? kMaxCapacity : capacity)) {}

void LogBuffer::push(const char* line) {
  // Truncate to kMaxLogLine - 1, then null-terminate.
  size_t len = std::strlen(line);
  if (len >= kMaxLogLine) len = kMaxLogLine - 1;
  std::memcpy(data_[head_], line, len);
  data_[head_][len] = '\0';

  head_ = (head_ + 1) % max_;
  if (count_ < max_)
    ++count_;
  // When full, head_ advances past the oldest entry, effectively dropping it.
}

void LogBuffer::push(const std::string& line) { push(line.c_str()); }

const char* LogBuffer::lineAt(size_t i) const {
  if (i >= count_) return nullptr;
  // Oldest entry is at (head_ - count_ + max_) % max_, then offset by i.
  size_t idx = (head_ - count_ + max_ + i) % max_;
  return data_[idx];
}

}  // namespace dpf
