#ifndef COMMON_FONT_HELPER_MACROS_H_
#define COMMON_FONT_HELPER_MACROS_H_

#define READ_STRING(OUT, D, O, L)                                \
  string_view OUT = ClippedSubstr(D, O, L);                      \
  if (OUT.length() != L) {                                       \
    return absl::InvalidArgumentError("Not enough input data."); \
  }

#define READ_UINT8(OUT, D, OFF)                    \
  uint8_t OUT = 0;                                 \
  {                                                \
    auto v = FontHelper::ReadUInt8(D.substr(OFF)); \
    if (!v.ok()) {                                 \
      return v.status();                           \
    }                                              \
    OUT = *v;                                      \
  }

#define READ_UINT16(OUT, D, OFF)                    \
  uint16_t OUT = 0;                                 \
  {                                                 \
    auto v = FontHelper::ReadUInt16(D.substr(OFF)); \
    if (!v.ok()) {                                  \
      return v.status();                            \
    }                                               \
    OUT = *v;                                       \
  }

#define READ_UINT24(OUT, D, OFF)                    \
  uint32_t OUT = 0;                                 \
  {                                                 \
    auto v = FontHelper::ReadUInt24(D.substr(OFF)); \
    if (!v.ok()) {                                  \
      return v.status();                            \
    }                                               \
    OUT = *v;                                       \
  }

#define READ_UINT32(OUT, D, OFF)                    \
  uint32_t OUT = 0;                                 \
  {                                                 \
    auto v = FontHelper::ReadUInt32(D.substr(OFF)); \
    if (!v.ok()) {                                  \
      return v.status();                            \
    }                                               \
    OUT = *v;                                       \
  }

#define READ_INT16(OUT, D, OFF)                    \
  int16_t OUT = 0;                                 \
  {                                                \
    auto v = FontHelper::ReadInt16(D.substr(OFF)); \
    if (!v.ok()) {                                 \
      return v.status();                           \
    }                                              \
    OUT = *v;                                      \
  }

#define READ_FIXED(OUT, D, OFF)                    \
  float OUT = 0.0f;                                \
  {                                                \
    auto v = FontHelper::ReadFixed(D.substr(OFF)); \
    if (!v.ok()) {                                 \
      return v.status();                           \
    }                                              \
    OUT = *v;                                      \
  }

#define WRITE_UINT8(V, O, M)                     \
  if (FontHelper::WillIntOverflow<uint8_t>(V)) { \
    return absl::InvalidArgumentError(M);        \
  }                                              \
  FontHelper::WriteUInt8(V, O);

#define WRITE_UINT16(V, O, M)                     \
  if (FontHelper::WillIntOverflow<uint16_t>(V)) { \
    return absl::InvalidArgumentError(M);         \
  }                                               \
  FontHelper::WriteUInt16(V, O);

// TODO(garretrieger) overflow check limits to 16 bit
#define WRITE_UINT24(V, O, M)                     \
  if (FontHelper::WillIntOverflow<uint16_t>(V)) { \
    return absl::InvalidArgumentError(M);         \
  }                                               \
  FontHelper::WriteUInt24(V, O);

#define WRITE_INT16(V, O, M)                     \
  if (FontHelper::WillIntOverflow<int16_t>(V)) { \
    return absl::InvalidArgumentError(M);        \
  }                                              \
  FontHelper::WriteInt16(V, O);

#define WRITE_INT24(V, O, M)                     \
  if (FontHelper::WillIntOverflow<int16_t>(V)) { \
    return absl::InvalidArgumentError(M);        \
  }                                              \
  FontHelper::WriteInt24(V, O);

#define WRITE_FIXED(V, O, M)              \
  if (FontHelper::WillFixedOverflow(V)) { \
    return absl::InvalidArgumentError(M); \
  }                                       \
  FontHelper::WriteFixed(V, O);

#endif  // COMMON_FONT_HELPER_MACROS_H_