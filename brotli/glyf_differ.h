#ifndef BROTLI_GLYF_DIFFER_H_
#define BROTLI_GLYF_DIFFER_H_

#include "absl/types/span.h"
#include "brotli/table_differ.h"

namespace brotli {

class GlyfDiffer : public TableDiffer {
 private:
  enum Mode {
    INIT = 0,
    NEW_DATA,
    EXISTING_DATA,
  } mode = INIT;

  absl::Span<const uint8_t> loca_;
  bool is_base_short_loca_;
  bool is_derived_short_loca_;

 public:
  GlyfDiffer(absl::Span<const uint8_t> loca, bool is_base_short_loca,
             bool is_derived_short_loca)
      : loca_(loca),
        is_base_short_loca_(is_base_short_loca),
        is_derived_short_loca_(is_derived_short_loca) {}

  void Process(unsigned derived_gid, unsigned base_gid,
               unsigned base_derived_gid, bool is_base_empty,
               unsigned* base_delta, /* OUT */
               unsigned* derived_delta /* OUT */) override {
    *derived_delta = GlyphLength(derived_gid);
    if (is_base_short_loca_ != is_derived_short_loca_) {
      // If loca's don't match then glyphs in the base may not use the same byte
      // alignment. so we can't blindly reference them. For now just treat all
      // glyphs as new data.
      //
      // Ideally the subsetter should ensure that a consistent loca format is
      // used in all subsets for optimal patch performance.
      mode = NEW_DATA;
      *base_delta = 0;
      return;
    }

    if (base_derived_gid == derived_gid) {
      mode = EXISTING_DATA;
      *base_delta = *derived_delta;
      return;
    }

    mode = NEW_DATA;
    *base_delta = 0;
  }

  void Finalize(unsigned* base_delta, /* OUT */
                unsigned* derived_delta /* OUT */) const override {
    // noop
    *base_delta = 0;
    *derived_delta = 0;
  }

  bool IsNewData() const override { return mode == NEW_DATA; }

 private:
  // Length of glyph (in bytes) found in the derived subset.
  unsigned GlyphLength(unsigned gid) {
    const uint8_t* derived_loca = loca_.data();

    if (is_derived_short_loca_) {
      size_t index = (size_t)gid * 2;
      if (index + 3 >= loca_.size()) {
        return 0;
      }

      unsigned off_1 = ((derived_loca[index] & 0xFF) << 8) |
                       (derived_loca[index + 1] & 0xFF);

      index += 2;
      unsigned off_2 = ((derived_loca[index] & 0xFF) << 8) |
                       (derived_loca[index + 1] & 0xFF);

      return (off_2 * 2) - (off_1 * 2);
    }

    size_t index = (size_t)gid * 4;
    if (index + 7 >= loca_.size()) {
      return 0;
    }

    unsigned off_1 = ((derived_loca[index] & 0xFF) << 24) |
                     ((derived_loca[index + 1] & 0xFF) << 16) |
                     ((derived_loca[index + 2] & 0xFF) << 8) |
                     (derived_loca[index + 3] & 0xFF);

    index += 4;
    unsigned off_2 = ((derived_loca[index] & 0xFF) << 24) |
                     ((derived_loca[index + 1] & 0xFF) << 16) |
                     ((derived_loca[index + 2] & 0xFF) << 8) |
                     (derived_loca[index + 3] & 0xFF);

    return off_2 - off_1;
  }
};

}  // namespace brotli

#endif  // BROTLI_GLYF_DIFFER_H_
