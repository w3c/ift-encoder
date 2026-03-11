#include "ift/common/brotli_binary_diff.h"

#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "brotli/shared_brotli_encoder.h"
#include "ift/common/font_data.h"

namespace ift::common {

using absl::Status;
using absl::string_view;
using brotli::DictionaryPointer;
using brotli::EncoderStatePointer;
using brotli::SharedBrotliEncoder;

Status BrotliBinaryDiff::Diff(const FontData& font_base,
                              const FontData& font_derived,
                              FontData* patch /* OUT */) const {
  std::vector<uint8_t> sink;
  sink.reserve(2 * (font_derived.size() - font_base.size()));

  Status sc = Diff(font_base, font_derived.str(), 0, true, sink);

  if (sc.ok()) {
    // TODO(grieger): eliminate this extra copy (have fontdata take ownership of
    // sink).
    patch->copy(reinterpret_cast<const char*>(sink.data()), sink.size());
  }

  return sc;
}

Status BrotliBinaryDiff::Diff(const FontData& font_base, string_view data,
                              unsigned stream_offset, bool is_last,
                              std::vector<uint8_t>& sink) const {
  // There's a decent amount of overhead in creating a dictionary, even if it's
  // completely empty. So don't set a dictionary unless it's non-empty.
  DictionaryPointer dictionary(nullptr, nullptr);
  if (font_base.size() > 0) {
    dictionary = SharedBrotliEncoder::CreateDictionary(font_base.span());
    if (!dictionary) {
      return absl::InternalError("Failed to create the shared dictionary.");
    }
  }

  // Don't give the encoder an estimated size if this is not all the data.
  unsigned data_size = !stream_offset && is_last ? data.size() : 0;
  EncoderStatePointer state = SharedBrotliEncoder::CreateEncoder(
      quality_, data_size, stream_offset, dictionary.get());
  if (!state) {
    return absl::InternalError("Failed to create the encoder.");
  }

  bool result =
      SharedBrotliEncoder::CompressToSink(data, is_last, state.get(), &sink);
  if (!result) {
    return absl::InternalError("Failed to encode brotli binary patch.");
  }

  return absl::OkStatus();
}

}  // namespace ift::common
