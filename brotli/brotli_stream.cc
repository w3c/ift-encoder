#include "brotli/brotli_stream.h"

#include <utility>

#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "brotli/shared_brotli_encoder.h"
#include "c/enc/command.h"

using absl::Span;
using absl::Status;

namespace brotli {

constexpr unsigned MAX_METABLOCK_SIZE = (1 << 24);

static unsigned num_of_postfix_bits(unsigned distance) {
  // Max distances worked out using the encoding scheme found in:
  // https://datatracker.ietf.org/doc/html/rfc7932#section-4
  if (distance <= 67108860) {
    return 0b00;
  } else if (distance <= 134217720) {
    return 0b01;
  } else if (distance <= 268435440) {
    return 0b10;
  } else {
    return 0b11;
  }
}

static uint16_t to_copy_code(unsigned length, uint16_t* num_extra_bits,
                             uint32_t* extra_bits) {
  // See: https://datatracker.ietf.org/doc/html/rfc7932#section-5
  constexpr unsigned code_to_extra_bits[] = {
      /* 0 */ 0,
      /* 1 */ 0,
      /* 2 */ 0,
      /* 3 */ 0,
      /* 4 */ 0,
      /* 5 */ 0,
      /* 6 */ 0,
      /* 7 */ 0,
      /* 8 */ 1,
      /* 9 */ 1,
      /* 10 */ 2,
      /* 11 */ 2,
      /* 12 */ 3,
      /* 13 */ 3,
      /* 14 */ 4,
      /* 15 */ 4,
      /* 16 */ 5,
      /* 17 */ 5,
      /* 18 */ 6,
      /* 19 */ 7,
      /* 20 */ 8,
      /* 21 */ 9,
      /* 22 */ 10,
      /* 23 */ 24,
  };

  uint16_t code = 0;
  unsigned max_length = 2;
  unsigned prev_max_length = 1;
  do {
    if (length <= max_length || code == 23) {
      *num_extra_bits = code_to_extra_bits[code];
      *extra_bits = length - prev_max_length - 1;
      return code;
    }

    code++;
    prev_max_length = max_length;
    max_length += (1 << code_to_extra_bits[code]);
  } while (true);
}

static unsigned insert_and_copy_code(unsigned copy_length,
                                     uint16_t* num_extra_bits,
                                     uint32_t* extra_bits) {
  // See: https://datatracker.ietf.org/doc/html/rfc7932#section-5
  uint16_t copy_code = to_copy_code(copy_length, num_extra_bits, extra_bits);
  uint16_t prefix = 0;
  if (copy_code <= 7) {
    prefix = 128;
  } else if (copy_code <= 15) {
    prefix = 192;
    copy_code -= 8;
  } else {
    prefix = 384;
    copy_code -= 16;
  }

  // Insert length is 0.
  return prefix | copy_code;
}

static void to_distance_code(unsigned distance, unsigned postfix_bits,
                             uint16_t* distance_code, uint16_t* num_extra_bits,
                             uint32_t* extra_bits) {
  uint16_t composite_distance;
  PrefixEncodeCopyDistance(distance + 15, 0, postfix_bits, &composite_distance,
                           extra_bits);
  *num_extra_bits = (composite_distance & 0b1111110000000000) >> 10;
  *distance_code = composite_distance & 0b0000001111111111;
}

bool BrotliStream::insert_from_dictionary(unsigned offset, unsigned length) {
  if (!length) {
    // no-op.
    return true;
  }

  if (length == 1) {
    // length of 1 can't be encoded as a backwards ref.
    return false;
  }

  // Backwards distance to the region in the dictionary starting at offset.
  unsigned distance =
      (dictionary_size_ + std::min(window_size_, uncompressed_size_)) - offset;

  if (!add_mlen(length)) {
    // Too big for one meta-block Break into multiple meta-blocks.
    unsigned remainder_length = length - MAX_METABLOCK_SIZE;
    if (remainder_length <= 1) {
      remainder_length = 2;
    }

    return insert_from_dictionary(offset, length - remainder_length) &&
           insert_from_dictionary(offset + (length - remainder_length),
                                  remainder_length);
  }

  unsigned postfix_bits = num_of_postfix_bits(distance);

  // Reference: https://datatracker.ietf.org/doc/html/rfc7932#section-9.2
  buffer_.append_number(0b0, 1);  // ISUNCOMPRESSED
  buffer_.append_number(0b0,
                        1);  // NBLTYPESL = 1 (number of literal block types)
  buffer_.append_number(
      0b0, 1);  // NBLTYPESI = 1 (number of insert+copy block types)
  buffer_.append_number(0b0,
                        1);  // NBLTYPESD = 1 (number of distance block types)

  buffer_.append_number(postfix_bits, 2);  // NPOSTFIX
  buffer_.append_number(0b0000, 4);        // NDIRECT

  buffer_.append_number(0b00, 2);  // Literal block type context mode
  buffer_.append_number(0b0,
                        1);  // NTREESL = 1 (number of literal prefix trees)
  buffer_.append_number(0b0,
                        1);  // NTREESD = 1 (number of literal prefix trees)

  // NTREESL prefix codes for literals:
  // we don't use any literals so just encode a 1 symbol tree with the zero
  // literal.
  add_prefix_tree(0, 8);

  // NBLTYPESI prefix codes for insert-and-copy lengths:
  uint16_t copy_num_extra_bits = 0;
  uint32_t copy_extra_bits = 0;
  uint16_t copy_code =
      insert_and_copy_code(length, &copy_num_extra_bits, &copy_extra_bits);
  add_prefix_tree(copy_code, 10);  // width = 10 since num codes = 704

  // NTREESD prefix codes for distances.
  uint16_t distance_code;
  uint16_t dist_num_extra_bits;
  uint32_t dist_extra_bits;
  unsigned distance_code_width =
      (unsigned)ceil(log(16 + (48 << postfix_bits)) / log(2));
  to_distance_code(distance, postfix_bits, &distance_code, &dist_num_extra_bits,
                   &dist_extra_bits);
  add_prefix_tree(distance_code, distance_code_width);

  // Command:
  // Insert and copy length: Code is omitted, just add the extra bits
  buffer_.append_number(copy_extra_bits, copy_num_extra_bits);

  // Literals (None).
  // Distance Code: Code is omitted, just add the extra bits
  buffer_.append_number(dist_extra_bits, dist_num_extra_bits);
  uncompressed_size_ += length;
  return true;
}

void BrotliStream::insert_uncompressed(absl::Span<const uint8_t> bytes) {
  uint32_t size = bytes.size();

  if (!add_mlen(size)) {
    // Too big for one meta-block Break into multiple meta-blocks.
    insert_uncompressed(bytes.subspan(0, MAX_METABLOCK_SIZE));
    insert_uncompressed(bytes.subspan(MAX_METABLOCK_SIZE));
    return;
  }

  // For meta-block header format see:
  // https://datatracker.ietf.org/doc/html/rfc7932#section-9.2
  buffer_.append_number(0b1, 1);  // ISUNCOMPRESSED
  buffer_.pad_to_end_of_byte();

  buffer_.append_raw(bytes);
  uncompressed_size_ += size;
}

Status BrotliStream::insert_compressed(Span<const uint8_t> bytes) {
  return insert_compressed_with_partial_dict(bytes, Span<const uint8_t>());
}

Status BrotliStream::insert_compressed_with_partial_dict(
    Span<const uint8_t> bytes, Span<const uint8_t> partial_dict) {
  if (!bytes.size()) {
    return absl::OkStatus();
  }

  if (partial_dict.size() > dictionary_size_) {
    partial_dict = partial_dict.subspan(0, dictionary_size_);
  }

  if (!uncompressed_size_ && dictionary_size_) {
    // If uncompressed size is zero but the dict is non-zero then
    // the brotli encoder call would add a stream header as would normally
    // be needed.
    add_stream_header();
  }

  // We need to ensure byte alignment since we're calling into
  // the regular brotli encoder which will start byte aligned.
  byte_align();

  DictionaryPointer dictionary(nullptr, nullptr);
  if (partial_dict.size() > 0) {
    dictionary = SharedBrotliEncoder::CreateDictionary(partial_dict);
    if (!dictionary) {
      return absl::InternalError("Failed to create brotli dictionary.");
    }
  }

  // dictionary_size is added to the stream offset so that static dictionary
  // references (which are window + dictionary size + static word id) will be
  // created with the right distance.
  unsigned stream_offset =
      uncompressed_size_ + dictionary_size_ - partial_dict.size();

  if (stream_offset > window_size_) {
    // this trick fails if stream_offset > window size since
    // internally the brotli encoder uses min(stream_offset,
    // window_size). To avoid this the window
    // size must always be > dict + uncompressed size.
    return absl::InternalError("stream offset exceeds window size.");
  }

  EncoderStatePointer state = create_encoder(stream_offset, dictionary.get());
  if (!state) {
    return absl::InternalError("Failed to create brotli encoder.");
  }

  bool result = SharedBrotliEncoder::CompressToSink(
      absl::string_view((const char*)bytes.data(), bytes.size()), false,
      state.get(), &buffer_.sink());
  if (!result) {
    return absl::InternalError("Failed to encode brotli binary patch.");
  }

  uncompressed_size_ += bytes.size();
  return absl::OkStatus();
}

// Align the stream to the nearest byte boundary.
void BrotliStream::byte_align() {
  if (buffer_.is_byte_aligned()) {
    return;
  }

  add_mlen(0);
}

void BrotliStream::end_stream() {
  buffer_.append_number(0b1, 1);  // ISLAST
  buffer_.append_number(0b1, 1);  // ISLASTEMPTY
  buffer_.pad_to_end_of_byte();
}

EncoderStatePointer BrotliStream::create_encoder(
    unsigned stream_offset,
    const BrotliEncoderPreparedDictionary* dictionary) const {
  EncoderStatePointer state =
      SharedBrotliEncoder::CreateEncoder(5, 0, stream_offset, dictionary);
  if (!state) {
    return state;
  }

  if (!BrotliEncoderSetParameter(state.get(), BROTLI_PARAM_LGWIN,
                                 window_bits_)) {
    LOG(WARNING) << "Failed to set brotli window size.";
    state.reset();
    return state;
  }

  return state;
}

bool BrotliStream::add_mlen(unsigned size) {
  uint32_t num_nibbles = 0;
  uint32_t num_nibbles_code = 0;
  if (size == 0) {
    // Empty meta-block
    buffer_.append_number(0b0, 1);   // ISLAST
    buffer_.append_number(0b11, 2);  // MNIBBLES
    buffer_.append_number(0b0, 1);   // Reserved
    buffer_.append_number(0b00, 2);  // MSKIPBYTES
    buffer_.pad_to_end_of_byte();
    return true;
  } else if (size <= (1 << 16)) {
    num_nibbles = 4;
    num_nibbles_code = 0b00;
  } else if (size <= (1 << 20)) {
    num_nibbles = 5;
    num_nibbles_code = 0b01;
  } else if (size <= MAX_METABLOCK_SIZE) {
    num_nibbles = 6;
    num_nibbles_code = 0b10;
  } else {
    // Too big for one meta-block signal need to break into multiple
    // meta-blocks.
    return false;
  }

  if (!uncompressed_size_) {
    add_stream_header();
  }

  // For meta-block header format see:
  // https://datatracker.ietf.org/doc/html/rfc7932#section-9.2
  buffer_.append_number(0b0, 1);                     // ISLAST
  buffer_.append_number(num_nibbles_code, 2);        // MNIBBLES
  buffer_.append_number(size - 1, num_nibbles * 4);  // MLEN - 1

  return true;
}

void BrotliStream::add_stream_header() {
  static std::pair<uint8_t, uint8_t> window_codes[] = {
      std::pair<uint8_t, uint8_t>(0b0100001, 7),  // 10
      std::pair<uint8_t, uint8_t>(0b0110001, 7),  // 11
      std::pair<uint8_t, uint8_t>(0b1000001, 7),  // 12
      std::pair<uint8_t, uint8_t>(0b1010001, 7),  // 13
      std::pair<uint8_t, uint8_t>(0b1100001, 7),  // 14
      std::pair<uint8_t, uint8_t>(0b1110001, 7),  // 15
      std::pair<uint8_t, uint8_t>(0b0, 1),        // 16
      std::pair<uint8_t, uint8_t>(0b0000001, 7),  // 17
      std::pair<uint8_t, uint8_t>(0b0011, 4),     // 18
      std::pair<uint8_t, uint8_t>(0b0101, 4),     // 19
      std::pair<uint8_t, uint8_t>(0b0111, 4),     // 20
      std::pair<uint8_t, uint8_t>(0b1001, 4),     // 21
      std::pair<uint8_t, uint8_t>(0b1011, 4),     // 22
      std::pair<uint8_t, uint8_t>(0b1101, 4),     // 23
      std::pair<uint8_t, uint8_t>(0b1111, 4),     // 24
  };

  std::pair<uint8_t, uint8_t> code = window_codes[window_bits_ - 10];
  buffer_.append_number(code.first, code.second);
}

void BrotliStream::add_prefix_tree(unsigned code, unsigned width) {
  buffer_.append_number(0b01, 2);      // Simple Tree
  buffer_.append_number(0b00, 2);      // NSYM = 1
  buffer_.append_number(code, width);  // Symbol 1
}

}  // namespace brotli
