#ifndef BROTLI_TABLE_RANGE_H_
#define BROTLI_TABLE_RANGE_H_

#include "absl/types/span.h"
#include "brotli/brotli_stream.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"

namespace brotli {

class TableRange {
 public:

  static unsigned table_offset(hb_face_t* face, hb_tag_t tag) {
    ift::common::CompareTableOffsets comparer(face);
    return comparer.table_offset(tag);
  }

  static size_t padded_table_size(size_t size) {
    while (size % 4) {
      size++;
    }
    return size;
  }

 public:
  TableRange(hb_face_t* base_face, hb_face_t* derived_face, hb_tag_t tag,
             const BrotliStream& base_stream) {

    derived_ = ift::common::FontHelper::TableData(derived_face, tag);

    out.reset(new BrotliStream(base_stream.window_bits(),
                               base_stream.dictionary_size(),
                               table_offset(derived_face, tag)));

    base_table_offset_ = table_offset(base_face, tag);
    tag_ = tag;
  }

 private:
  ift::common::FontData derived_;

  unsigned base_table_offset_;
  unsigned base_offset_ = 0;
  unsigned derived_offset_ = 0;
  unsigned base_length_ = 0;
  unsigned derived_length_ = 0;
  std::unique_ptr<BrotliStream> out;
  hb_tag_t tag_;

 public:
  hb_tag_t tag() const { return tag_; }

  BrotliStream& stream() { return *out; }

  const uint8_t* data() { return reinterpret_cast<const uint8_t*>(derived_.data()); }

  unsigned length() { return derived_.size(); }

  void Extend(unsigned base_length, unsigned derived_length) {
    base_length_ += base_length;
    derived_length_ += derived_length;
  }

  absl::Status CommitNew() {
    absl::Status s = out->insert_compressed(absl::Span<const uint8_t>(
        data() + derived_offset_, derived_length_));
    if (!s.ok()) {
      return s;
    }

    derived_offset_ += derived_length_;
    base_offset_ += base_length_;

    base_length_ = 0;
    derived_length_ = 0;

    return absl::OkStatus();
  }

  void CommitExisting() {
    if (!out->insert_from_dictionary(base_table_offset_ + base_offset_,
                                     derived_length_)) {
      // 1 byte backwards refs must be inserted as literals.
      out->insert_uncompressed(absl::Span<const uint8_t>(
          data() + derived_offset_, derived_length_));
    }

    derived_offset_ += derived_length_;
    base_offset_ += base_length_;

    base_length_ = 0;
    derived_length_ = 0;
  }
};

}  // namespace brotli

#endif  // BROTLI_TABLE_RANGE_H_
