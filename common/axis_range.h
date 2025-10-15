#ifndef COMMON_AXIS_RANGE_H_
#define COMMON_AXIS_RANGE_H_

#include <optional>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace common {

struct AxisRange {
  static AxisRange Point(float point) { return AxisRange(point, point); }

  static absl::StatusOr<AxisRange> Range(float start, float end) {
    if (end < start) {
      return absl::InvalidArgumentError(
          absl::StrCat("end (", end, ") is less than start (", start, ")"));
    }
    return AxisRange(start, end);
  }

  friend void PrintTo(const AxisRange& point, std::ostream* os);

  template <typename H>
  friend H AbslHashValue(H h, const AxisRange& s) {
    return H::combine(std::move(h), s.start_, s.end_);
  }

  bool operator==(const AxisRange& other) const {
    return other.start_ == start_ && other.end_ == end_;
  }

  bool Intersects(const AxisRange& other) const {
    return other.end_ >= start_ && end_ >= other.start_;
  }

  std::optional<AxisRange> Intersection(const AxisRange& other) const {
    if (!Intersects(other)) {
      return std::nullopt;
    }

    return AxisRange(std::max(start_, other.start_),
                     std::min(end_, other.end_));
  }

  AxisRange() : start_(0), end_(0) {}

  float start() const { return start_; }
  float end() const { return end_; }
  bool IsPoint() const { return start_ == end_; }
  bool IsRange() const { return start_ != end_; }

 private:
  float start_;
  float end_;
  AxisRange(float start, float end) : start_(start), end_(end) {}
};

}  // namespace common

#endif  // COMMON_AXIS_RANGE_H_