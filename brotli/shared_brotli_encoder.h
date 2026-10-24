#ifndef BROTLI_SHARED_BROTLI_ENCODER_H_
#define BROTLI_SHARED_BROTLI_ENCODER_H_

#include <memory>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "brotli/brotli_arena.h"
#include "brotli/encode.h"

namespace brotli {

typedef std::unique_ptr<BrotliEncoderState,
                        decltype(&BrotliEncoderDestroyInstance)>
    EncoderStatePointer;
typedef std::unique_ptr<BrotliEncoderPreparedDictionary,
                        decltype(&BrotliEncoderDestroyPreparedDictionary)>
    DictionaryPointer;

/* A collection of utilities that ease using the existing brotli encoder API. */
class SharedBrotliEncoder {
 public:
  static DictionaryPointer CreateDictionary(absl::Span<const uint8_t> data,
                                            BrotliArena* arena = nullptr) {
    brotli_alloc_func alloc_func = arena ? &BrotliArena::Alloc : nullptr;
    brotli_free_func free_func = arena ? &BrotliArena::Free : nullptr;
    return DictionaryPointer(
        BrotliEncoderPrepareDictionary(
            BROTLI_SHARED_DICTIONARY_RAW, data.size(), data.data(),
            BROTLI_MAX_QUALITY, alloc_func, free_func, arena),
        &BrotliEncoderDestroyPreparedDictionary);
  }

  static EncoderStatePointer CreateEncoder(
      unsigned quality, size_t font_size, unsigned stream_offset,
      const BrotliEncoderPreparedDictionary* dictionary,
      BrotliArena* arena = nullptr) {
    brotli_alloc_func alloc_func = arena ? &BrotliArena::Alloc : nullptr;
    brotli_free_func free_func = arena ? &BrotliArena::Free : nullptr;
    EncoderStatePointer state = EncoderStatePointer(
        BrotliEncoderCreateInstance(alloc_func, free_func, arena),
        &BrotliEncoderDestroyInstance);

    if (!BrotliEncoderSetParameter(state.get(), BROTLI_PARAM_QUALITY,
                                   quality)) {
      LOG(WARNING) << "Failed to set brotli quality.";
      return EncoderStatePointer(nullptr, nullptr);
    }

    if (font_size && !BrotliEncoderSetParameter(
                         state.get(), BROTLI_PARAM_SIZE_HINT, font_size)) {
      LOG(WARNING) << "Failed to set brotli size hint.";
      return EncoderStatePointer(nullptr, nullptr);
    }

    if (dictionary &&
        !BrotliEncoderAttachPreparedDictionary(state.get(), dictionary)) {
      LOG(WARNING) << "Failed to attach dictionary.";
      return EncoderStatePointer(nullptr, nullptr);
    }

    if (!BrotliEncoderSetParameter(state.get(), BROTLI_PARAM_MODE,
                                   BROTLI_MODE_FONT)) {
      LOG(WARNING) << "Failed to set brotli mode.";
      return EncoderStatePointer(nullptr, nullptr);
    }

    if (stream_offset &&
        !BrotliEncoderSetParameter(state.get(), BROTLI_PARAM_STREAM_OFFSET,
                                   stream_offset)) {
      LOG(WARNING) << "Failed to set brotli stream offset.";
      return EncoderStatePointer(nullptr, nullptr);
    }

    return state;
  }

  static bool CompressToSink(absl::string_view derived, bool is_last,
                             BrotliEncoderState* state, /* OUT */
                             std::vector<uint8_t>* sink /* OUT */) {
    const BrotliEncoderOperation final_op =
        is_last ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_FLUSH;

    unsigned int source_index = 0;
    BrotliEncoderOperation current_op = BROTLI_OPERATION_PROCESS;
    size_t available_in, available_out = 0;
    BROTLI_BOOL result = BROTLI_TRUE;
    while (result && (source_index < derived.size() ||
                      !IsFinished(state, current_op, is_last))) {
      const absl::string_view sp = derived.substr(source_index);
      available_in = sp.size();
      const uint8_t* next_in =
          available_in ? reinterpret_cast<const uint8_t*>(sp.data()) : nullptr;
      current_op = available_in ? BROTLI_OPERATION_PROCESS : final_op;
      result = BrotliEncoderCompressStream(state, current_op, &available_in,
                                           &next_in, &available_out, nullptr,
                                           nullptr);
      size_t buffer_size = 0;
      const uint8_t* buffer = BrotliEncoderTakeOutput(state, &buffer_size);
      if (buffer_size > 0) {
        Append(buffer, buffer_size, sink);
      }
      source_index += sp.size() - available_in;
    }

    return bool(result);
  }

 private:
  static void Append(const uint8_t* buffer, size_t buffer_size,
                     std::vector<uint8_t>* sink) {
    sink->insert(sink->end(), buffer, buffer + buffer_size);
  }

  static bool IsFinished(BrotliEncoderState* state,
                         BrotliEncoderOperation current_op, bool is_last) {
    if (current_op == BROTLI_OPERATION_PROCESS) return false;

    if (is_last) return BrotliEncoderIsFinished(state);

    return !BrotliEncoderHasMoreOutput(state);
  }
};

}  // namespace brotli

#endif  // BROTLI_SHARED_BROTLI_ENCODER_H_
