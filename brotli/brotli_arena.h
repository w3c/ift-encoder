#ifndef BROTLI_BROTLI_ARENA_H_
#define BROTLI_BROTLI_ARENA_H_

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

#include "absl/log/log.h"

namespace brotli {

/* Reusable bump-pointer arena for Brotli encoder and dictionary allocations. */
class BrotliArena {
 public:
  static constexpr size_t kAlignment = alignof(std::max_align_t);
  static_assert(kAlignment >= sizeof(size_t));

  explicit BrotliArena(size_t capacity = 256u * 1024u * 1024u)
      : capacity_(capacity), offset_(0), buf_(nullptr) {}

  BrotliArena(const BrotliArena&) = delete;
  BrotliArena& operator=(const BrotliArena&) = delete;
  BrotliArena(BrotliArena&&) = default;
  BrotliArena& operator=(BrotliArena&&) = default;

  static void* Alloc(void* opaque, size_t size) {
    if (!opaque) {
      return nullptr;
    }
    auto* self = static_cast<BrotliArena*>(opaque);
    if (!self->buf_) {
      self->buf_.reset(new uint8_t[self->capacity_]);
    }
    // Round size up to multiple of kAlignment bytes for proper alignment.
    size_t payload_size = (size + kAlignment - 1) & ~(kAlignment - 1);
    size_t total_size = payload_size + kAlignment;
    if (self->offset_ < self->capacity_ &&
        total_size <= self->capacity_ - self->offset_) {
      *reinterpret_cast<size_t*>(self->buf_.get() + self->offset_) =
          payload_size;
      void* ptr = self->buf_.get() + self->offset_ + kAlignment;
      self->offset_ += total_size;
      return ptr;
    }
    // Fall back to heap allocation if arena capacity is exceeded.
    LOG(WARNING) << "BrotliArena capacity exceeded (requested " << size
                 << " bytes at offset " << self->offset_ << " of "
                 << self->capacity_ << "), falling back to new.";
    self->fallback_allocs_.emplace_back(new uint8_t[size]);
    return self->fallback_allocs_.back().get();
  }

  static void Free(void* opaque, void* address) {
    if (!address || !opaque) {
      return;
    }
    auto* self = static_cast<BrotliArena*>(opaque);
    auto addr_val = reinterpret_cast<std::uintptr_t>(address);
    auto buf_start = reinterpret_cast<std::uintptr_t>(self->buf_.get());
    if (self->buf_ && addr_val >= buf_start + kAlignment &&
        addr_val <= buf_start + self->offset_) {
      size_t alloc_offset = addr_val - buf_start;
      size_t payload_size = *reinterpret_cast<const size_t*>(
          self->buf_.get() + (alloc_offset - kAlignment));
      if (alloc_offset + payload_size == self->offset_) {
        self->offset_ = alloc_offset - kAlignment;
      }
      return;
    }
    for (auto it = self->fallback_allocs_.rbegin();
         it != self->fallback_allocs_.rend(); ++it) {
      if (it->get() == address) {
        self->fallback_allocs_.erase(std::next(it).base());
        return;
      }
    }
  }

  void Reset() {
    offset_ = 0;
    fallback_allocs_.clear();
  }

  size_t offset() const { return offset_; }
  size_t capacity() const { return capacity_; }

 private:
  size_t capacity_;
  size_t offset_;
  std::unique_ptr<uint8_t[]> buf_;
  std::vector<std::unique_ptr<uint8_t[]>> fallback_allocs_;
};

}  // namespace brotli

#endif  // BROTLI_BROTLI_ARENA_H_
