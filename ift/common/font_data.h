#ifndef COMMON_FONT_DATA_H_
#define COMMON_FONT_DATA_H_

#include <cstring>
#include <memory>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "hb.h"

namespace ift::common {

typedef std::unique_ptr<hb_face_t, decltype(&hb_face_destroy)>
    hb_face_unique_ptr;
typedef std::unique_ptr<hb_font_t, decltype(&hb_font_destroy)>
    hb_font_unique_ptr;
typedef std::unique_ptr<hb_blob_t, decltype(&hb_blob_destroy)>
    hb_blob_unique_ptr;

static hb_face_unique_ptr make_hb_face(hb_face_t* face) {
  return hb_face_unique_ptr(face, &hb_face_destroy);
}

static hb_font_unique_ptr make_hb_font(hb_font_t* font) {
  return hb_font_unique_ptr(font, &hb_font_destroy);
}

static hb_face_unique_ptr make_hb_face_builder() {
  return hb_face_unique_ptr(hb_face_builder_create(), &hb_face_destroy);
}

static hb_blob_unique_ptr make_hb_blob(hb_blob_t* blob) {
  return hb_blob_unique_ptr(blob, &hb_blob_destroy);
}

static hb_blob_unique_ptr make_hb_blob() {
  return hb_blob_unique_ptr(hb_blob_get_empty(), &hb_blob_destroy);
}

// Holds the binary data for a font.
class FontData {
 public:
  FontData() : buffer_(make_hb_blob()), saved_face_(make_hb_face(nullptr)) {}

  // TODO(garretrieger): construct from span

  explicit FontData(hb_blob_t* blob)
      : buffer_(make_hb_blob()), saved_face_(make_hb_face(nullptr)) {
    set(blob);
  }

  explicit FontData(hb_face_t* face)
      : buffer_(make_hb_blob()), saved_face_(make_hb_face(nullptr)) {
    set(face);
  }

  explicit FontData(hb_blob_unique_ptr blob)
      : buffer_(std::move(blob)), saved_face_(make_hb_face(nullptr)) {}

  explicit FontData(hb_face_unique_ptr face)
      : buffer_(make_hb_blob(hb_face_reference_blob(face.get()))),
        saved_face_(std::move(face)) {}

  explicit FontData(::absl::string_view data)
      : buffer_(make_hb_blob()), saved_face_(make_hb_face(nullptr)) {
    copy(data);
  }

  FontData(const FontData&) = delete;

  FontData(FontData&& other)
      : buffer_(make_hb_blob(nullptr)), saved_face_(make_hb_face(nullptr)) {
    buffer_ = std::move(other.buffer_);
    saved_face_ = std::move(other.saved_face_);

    other.buffer_ = make_hb_blob();
    other.saved_face_ = make_hb_face(nullptr);
  }

  FontData& operator=(const FontData&) = delete;

  FontData& operator=(FontData&& other) {
    if (this == &other) {
      return *this;
    }
    reset();

    buffer_ = std::move(other.buffer_);
    saved_face_ = std::move(other.saved_face_);

    other.buffer_ = make_hb_blob();
    other.saved_face_ = make_hb_face(nullptr);

    return *this;
  }

  ~FontData() { reset(); }

  bool operator==(const FontData& other) const { return str() == other.str(); }

  bool empty() const { return size() == 0; }

  // TODO(garretrieger): add a to_span method.

  ::absl::Span<const uint8_t> span() const {
    return ::absl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(data()),
                                       size());
  }

  ::absl::string_view str() const {
    return ::absl::string_view(data(), size());
  }

  ::absl::string_view str(unsigned int start) const {
    if (start >= size()) {
      return ::absl::string_view();
    }
    return ::absl::string_view(data() + start, size() - start);
  }

  // From [start, end)
  ::absl::string_view str(unsigned int start, unsigned int end) const {
    if (start >= size() || start >= end) {
      return ::absl::string_view();
    }
    if (end > size()) {
      end = size();
    }
    return ::absl::string_view(data() + start, end - start);
  }

  std::string string() const { return std::string(str()); }

  void set(hb_blob_t* blob) {
    reset();
    buffer_ = make_hb_blob(hb_blob_reference(blob));
  }

  void set(hb_face_t* face) {
    reset();

    saved_face_ = make_hb_face(hb_face_reference(face));
    buffer_ = make_hb_blob(hb_face_reference_blob(face));
  }

  void set(hb_face_t* face, hb_blob_t* blob) {
    reset();

    saved_face_ = make_hb_face(hb_face_reference(face));
    buffer_ = make_hb_blob(hb_blob_reference(blob));
  }

  void shallow_copy(const FontData& other) {
    if (other.saved_face_) {
      set(other.saved_face_.get());
    } else {
      set(other.buffer_.get());
    }
  }

  // TODO(garretrieger): copy method which takes vector<uint8_t>.
  // TODO(garretgrieger): method which takes ownership of a vector<uint8_t>

  void copy(const char* data, unsigned int length) {
    reset();
    char* buffer = reinterpret_cast<char*>(malloc(length));
    memcpy(buffer, data, length);
    buffer_ = make_hb_blob(
        hb_blob_create(buffer, length, HB_MEMORY_MODE_READONLY, buffer, &free));
  }

  void copy(::absl::string_view data) { copy(data.data(), data.size()); }

  void reset() {
    if (buffer_.get() != hb_blob_get_empty()) {
      buffer_ = make_hb_blob();
    }

    if (saved_face_.get() != nullptr) {
      saved_face_ = make_hb_face(nullptr);
    }
  }

  hb_face_unique_ptr face() const { return make_hb_face(reference_face()); }

  hb_blob_unique_ptr blob() const { return make_hb_blob(reference_blob()); }

  hb_face_t* reference_face() const {
    if (saved_face_) {
      return hb_face_reference(saved_face_.get());
    }
    return hb_face_create(buffer_.get(), 0);
  }

  hb_blob_t* reference_blob() const { return hb_blob_reference(buffer_.get()); }

  const char* data() const {
    unsigned int size;
    return hb_blob_get_data(buffer_.get(), &size);
  }

  unsigned int size() const { return hb_blob_get_length(buffer_.get()); }

 private:
  hb_blob_unique_ptr buffer_;
  hb_face_unique_ptr saved_face_;
};

}  // namespace ift::common

#endif  // COMMON_FONT_DATA_H_
