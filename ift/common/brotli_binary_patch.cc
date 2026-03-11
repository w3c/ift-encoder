#include "ift/common/brotli_binary_patch.h"

#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "brotli/decode.h"
#include "ift/common/binary_patch.h"
#include "ift/common/font_data.h"

typedef std::unique_ptr<BrotliDecoderState,
                        decltype(&BrotliDecoderDestroyInstance)>
    DecoderStatePointer;

namespace ift::common {

using absl::Status;
using absl::string_view;

DecoderStatePointer CreateDecoder(const FontData& base) {
  DecoderStatePointer state = DecoderStatePointer(
      BrotliDecoderCreateInstance(nullptr, nullptr, nullptr),
      &BrotliDecoderDestroyInstance);

  if (!BrotliDecoderAttachDictionary(
          state.get(), BROTLI_SHARED_DICTIONARY_RAW, base.size(),
          reinterpret_cast<const uint8_t*>(base.data()))) {
    LOG(WARNING) << "Failed to attach dictionary to the decoder.";
    return DecoderStatePointer(nullptr, nullptr);
  }

  return state;
}

static void Append(const uint8_t* buffer, size_t buffer_size,
                   std::vector<uint8_t>* sink) {
  sink->insert(sink->end(), buffer, buffer + buffer_size);
}

Status DecompressToSink(const FontData& patch,
                        BrotliDecoderState* state, /* OUT */
                        std::vector<uint8_t>* sink /* OUT */) {
  unsigned int source_index = 0;

  size_t available_in, available_out = 0;
  BrotliDecoderResult result = BROTLI_DECODER_RESULT_SUCCESS;
  while (source_index < patch.size() && result != BROTLI_DECODER_RESULT_ERROR) {
    string_view sp = patch.str(source_index);
    available_in = sp.size();
    const uint8_t* next_in = reinterpret_cast<const uint8_t*>(sp.data());
    result = BrotliDecoderDecompressStream(state, &available_in, &next_in,
                                           &available_out, nullptr, nullptr);
    source_index += sp.size() - available_in;
    size_t buffer_size = 0;
    const uint8_t* buffer = BrotliDecoderTakeOutput(state, &buffer_size);
    if (buffer_size > 0) {
      Append(buffer, buffer_size, sink);
    } else if (result == BROTLI_DECODER_RESULT_SUCCESS) {
      // Decoding is finished and all output is pushed.
      break;
    }
  }

  while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
    available_in = 0;
    const uint8_t* next_in = nullptr;
    result = BrotliDecoderDecompressStream(state, &available_in, &next_in,
                                           &available_out, nullptr, nullptr);
    size_t buffer_size = 0;
    const uint8_t* buffer = BrotliDecoderTakeOutput(state, &buffer_size);
    if (buffer_size > 0) {
      Append(buffer, buffer_size, sink);
    }
  }

  if (result != BROTLI_DECODER_RESULT_SUCCESS || source_index != patch.size()) {
    return absl::InternalError("Brotli decoder failed.");
  }
  return absl::OkStatus();
}

Status BrotliBinaryPatch::Patch(const FontData& font_base,
                                const FontData& patch,
                                FontData* font_derived /* OUT */) const {
  DecoderStatePointer state = CreateDecoder(font_base);
  if (!state) {
    return absl::InternalError("Decoder creation failed.");
  }

  // TODO(garretrieger): better default size calculation.
  std::vector<uint8_t> sink;
  sink.reserve(font_base.size() + patch.size());
  Status sc = DecompressToSink(patch, state.get(), &sink);
  if (!sc.ok()) {
    return sc;
  }

  font_derived->copy(reinterpret_cast<const char*>(sink.data()), sink.size());

  return absl::OkStatus();
}

Status BrotliBinaryPatch::Patch(const FontData& font_base,
                                const std::vector<FontData>& patch,
                                FontData* font_derived) const {
  if (patch.size() == 1) {
    return Patch(font_base, patch[0], font_derived);
  }

  if (patch.size() == 0) {
    return absl::InvalidArgumentError("Must provide at least one patch.");
  }

  return absl::InvalidArgumentError(
      "Brotli binary patches cannot be applied independently");
}

}  // namespace ift::common
