#include "ift/glyph_keyed_diff.h"

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "common/brotli_binary_patch.h"
#include "common/compat_id.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "gtest/gtest.h"
#include "hb-subset.h"
#include "hb.h"
#include "ift/proto/ift_table.h"

using absl::flat_hash_set;
using absl::StatusOr;
using absl::StrCat;
using absl::string_view;
using common::BrotliBinaryPatch;
using common::CompatId;
using common::FontData;
using common::FontHelper;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::hb_font_unique_ptr;
using common::make_hb_blob;
using common::make_hb_face;
using common::make_hb_font;
using ift::proto::IFTTable;

const uint8_t data_stream_u16_short_loca[] = {
    // num header bytes = 37
    0x00, 0x00, 0x00, 0x04,  // glyphCount
    0x01,                    // table count

    // glyphIds[4]
    0x00, 0x25,  // gid 37
    0x00, 0x28,  // gid 40
    0x00, 0x49,  // gid 73
    0x00, 0x5b,  // gid 91

    // tables[1]
    'g', 'l', 'y', 'f',

    // offset stream
    0x00, 0x00, 0x00, 0x25,  // gid 37

    0x00, 0x00, 0x00, 0xb1,  // gid 40
    0x00, 0x00, 0x00, 0xb1,  // gid 73
    0x00, 0x00, 0x01, 0x83,  // gid 91
    0x00, 0x00, 0x02, 0x1f,  // end

    // gid 37 - A (140 bytes)
    0x00, 0x02, 0x00, 0x1c, 0x00, 0x00, 0x05, 0x1d, 0x05, 0xb0, 0x00, 0x07,
    0x00, 0x0a, 0x00, 0x54, 0xb2, 0x0a, 0x0b, 0x0c, 0x11, 0x12, 0x39, 0xb0,
    0x0a, 0x10, 0xb0, 0x04, 0xd0, 0x00, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x04,
    0x2f, 0x1b, 0xb1, 0x04, 0x1e, 0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0,
    0x02, 0x2f, 0x1b, 0xb1, 0x02, 0x12, 0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58,
    0xb0, 0x06, 0x2f, 0x1b, 0xb1, 0x06, 0x12, 0x3e, 0x59, 0xb2, 0x08, 0x04,
    0x02, 0x11, 0x12, 0x39, 0xb0, 0x08, 0x2f, 0xb1, 0x00, 0x01, 0xb0, 0x0a,
    0x2b, 0x58, 0x21, 0xd8, 0x1b, 0xf4, 0x59, 0xb2, 0x0a, 0x04, 0x02, 0x11,
    0x12, 0x39, 0x30, 0x31, 0x01, 0x21, 0x03, 0x23, 0x01, 0x33, 0x01, 0x23,
    0x01, 0x21, 0x03, 0x03, 0xcd, 0xfd, 0x9e, 0x89, 0xc6, 0x02, 0x2c, 0xa8,
    0x02, 0x2d, 0xc5, 0xfd, 0x4d, 0x01, 0xef, 0xf8, 0x01, 0x7c, 0xfe, 0x84,
    0x05, 0xb0, 0xfa, 0x50, 0x02, 0x1a, 0x02, 0xa9,
    // gid 73 - e (210 bytes)
    0x00, 0x02, 0x00, 0x5d, 0xff, 0xec, 0x03, 0xf3, 0x04, 0x4e, 0x00, 0x15,
    0x00, 0x1d, 0x00, 0x6c, 0xb2, 0x08, 0x1e, 0x1f, 0x11, 0x12, 0x39, 0xb0,
    0x08, 0x10, 0xb0, 0x16, 0xd0, 0x00, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x08,
    0x2f, 0x1b, 0xb1, 0x08, 0x1a, 0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0,
    0x00, 0x2f, 0x1b, 0xb1, 0x00, 0x12, 0x3e, 0x59, 0xb2, 0x1a, 0x08, 0x00,
    0x11, 0x12, 0x39, 0xb0, 0x1a, 0x2f, 0xb4, 0xbf, 0x1a, 0xcf, 0x1a, 0x02,
    0x5d, 0xb1, 0x0c, 0x01, 0xb0, 0x0a, 0x2b, 0x58, 0x21, 0xd8, 0x1b, 0xf4,
    0x59, 0xb0, 0x00, 0x10, 0xb1, 0x10, 0x01, 0xb0, 0x0a, 0x2b, 0x58, 0x21,
    0xd8, 0x1b, 0xf4, 0x59, 0xb2, 0x13, 0x08, 0x00, 0x11, 0x12, 0x39, 0xb0,
    0x08, 0x10, 0xb1, 0x16, 0x01, 0xb0, 0x0a, 0x2b, 0x58, 0x21, 0xd8, 0x1b,
    0xf4, 0x59, 0x30, 0x31, 0x05, 0x22, 0x00, 0x35, 0x35, 0x34, 0x36, 0x36,
    0x33, 0x32, 0x12, 0x11, 0x15, 0x21, 0x16, 0x16, 0x33, 0x32, 0x36, 0x37,
    0x17, 0x06, 0x01, 0x22, 0x06, 0x07, 0x21, 0x35, 0x26, 0x26, 0x02, 0x4d,
    0xdc, 0xfe, 0xec, 0x7b, 0xdd, 0x81, 0xd3, 0xea, 0xfd, 0x23, 0x04, 0xb3,
    0x8a, 0x62, 0x88, 0x33, 0x71, 0x88, 0xfe, 0xd9, 0x70, 0x98, 0x12, 0x02,
    0x1e, 0x08, 0x88, 0x14, 0x01, 0x21, 0xf2, 0x22, 0xa1, 0xfd, 0x8f, 0xfe,
    0xea, 0xfe, 0xfd, 0x4d, 0xa0, 0xc5, 0x50, 0x42, 0x58, 0xd1, 0x03, 0xca,
    0xa3, 0x93, 0x0e, 0x8d, 0x9b, 0x00,
    // gid 91 - w (156 bytes)
    0x00, 0x01, 0x00, 0x2b, 0x00, 0x00, 0x05, 0xd3, 0x04, 0x3a, 0x00, 0x0c,
    0x00, 0x60, 0xb2, 0x05, 0x0d, 0x0e, 0x11, 0x12, 0x39, 0x00, 0xb0, 0x00,
    0x45, 0x58, 0xb0, 0x01, 0x2f, 0x1b, 0xb1, 0x01, 0x1a, 0x3e, 0x59, 0xb0,
    0x00, 0x45, 0x58, 0xb0, 0x08, 0x2f, 0x1b, 0xb1, 0x08, 0x1a, 0x3e, 0x59,
    0xb0, 0x00, 0x45, 0x58, 0xb0, 0x0b, 0x2f, 0x1b, 0xb1, 0x0b, 0x1a, 0x3e,
    0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x03, 0x2f, 0x1b, 0xb1, 0x03, 0x12,
    0x3e, 0x59, 0xb0, 0x00, 0x45, 0x58, 0xb0, 0x06, 0x2f, 0x1b, 0xb1, 0x06,
    0x12, 0x3e, 0x59, 0xb2, 0x00, 0x0b, 0x03, 0x11, 0x12, 0x39, 0xb2, 0x05,
    0x0b, 0x03, 0x11, 0x12, 0x39, 0xb2, 0x0a, 0x0b, 0x03, 0x11, 0x12, 0x39,
    0x30, 0x31, 0x25, 0x13, 0x33, 0x01, 0x23, 0x01, 0x01, 0x23, 0x01, 0x33,
    0x13, 0x13, 0x33, 0x04, 0x4a, 0xd0, 0xb9, 0xfe, 0xc5, 0x96, 0xfe, 0xf9,
    0xff, 0x00, 0x96, 0xfe, 0xc6, 0xb8, 0xd5, 0xfc, 0x95, 0xff, 0x03, 0x3b,
    0xfb, 0xc6, 0x03, 0x34, 0xfc, 0xcc, 0x04, 0x3a, 0xfc, 0xd6, 0x03, 0x2a};

const uint8_t data_stream_u16_cff[57] = {
    // num header bytes = 37
    0x00, 0x00, 0x00, 0x01,  // glyphCount
    0x01,                    // table count

    // glyphIds[1]
    0x00, 0x02,  // gid 2

    // tables[1]
    'C', 'F', 'F', ' ',

    // offset stream
    0x00, 0x00, 0x00, 0x13,  // gid 2
    0x00, 0x00, 0x00, 0x39,  // end (+38 bytes)

    // gid 2 - A (38 bytes)
    0xfb, 0xcc, 0xf7, 0x09, 0x0a, 0xf7, 0x07, 0x0a, 0x13, 0x50, 0x8f, 0x0a,
    0x13, 0x60, 0xb6, 0xfd, 0x26, 0x15, 0xb0, 0xa8, 0xa7, 0xb4, 0x1f, 0x13,
    0xa0, 0xb4, 0x6e, 0xa9, 0x66, 0x67, 0x6d, 0x6d, 0x62, 0x1e, 0x13, 0x60,
    0xf4, 0x0a};

namespace ift {

class GlyphKeyedDiffTest : public ::testing::Test {
 protected:
  GlyphKeyedDiffTest() {
    font = from_file("ift/testdata/NotoSansJP-Regular.ift.ttf");
    original = from_file("ift/testdata/NotoSansJP-Regular.subset.ttf");
    roboto = from_file("common/testdata/Roboto-Regular.Awesome.ttf");
    roboto_vf = from_file("common/testdata/Roboto[wdth,wght].abcd.ttf");
    noto_sans_jp_cff = from_file("common/testdata/NotoSansJP-Regular.otf");
  }

  FontData from_file(const char* filename) {
    return FontData(make_hb_blob(hb_blob_create_from_file(filename)));
  }

  FontData Subset(const FontData& font, flat_hash_set<uint32_t> gids) {
    hb_face_unique_ptr face = font.face();
    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    for (uint32_t gid : gids) {
      hb_set_add(hb_subset_input_glyph_set(input), gid);
    }
    hb_subset_input_set_flags(input,
                              HB_SUBSET_FLAGS_RETAIN_GIDS |
                                  HB_SUBSET_FLAGS_PASSTHROUGH_UNRECOGNIZED |
                                  HB_SUBSET_FLAGS_NOTDEF_OUTLINE);

    hb_subset_plan_t* plan = hb_subset_plan_create_or_fail(face.get(), input);
    hb_subset_input_destroy(input);

    hb_face_unique_ptr subset =
        make_hb_face(hb_subset_plan_execute_or_fail(plan));
    hb_subset_plan_destroy(plan);

    hb_blob_unique_ptr subset_data =
        make_hb_blob(hb_face_reference_blob(subset.get()));

    FontData result(std::move(subset_data));
    return result;
  }

  FontData font;
  FontData original;
  FontData roboto;
  FontData roboto_vf;
  FontData noto_sans_jp_cff;
  BrotliBinaryPatch unbrotli;
};

StatusOr<uint32_t> loca_value(string_view loca, hb_codepoint_t index) {
  string_view entry = loca.substr(index * 4, 4);
  return FontHelper::ReadUInt32(entry);
}

StatusOr<uint32_t> glyph_size(const FontData& font_data,
                              hb_codepoint_t codepoint) {
  hb_face_unique_ptr face = font_data.face();
  hb_font_unique_ptr font = make_hb_font(hb_font_create(face.get()));

  hb_codepoint_t gid;
  hb_font_get_nominal_glyph(font.get(), codepoint, &gid);
  if (gid == 0) {
    return absl::NotFoundError(StrCat("No cmap for ", codepoint));
  }

  auto loca = FontHelper::Loca(face.get());
  if (!loca.ok()) {
    return loca.status();
  }

  auto start = loca_value(*loca, gid);
  auto end = loca_value(*loca, gid + 1);

  if (!start.ok()) {
    return start.status();
  }
  if (!end.ok()) {
    return start.status();
  }

  return *end - *start;
}

TEST_F(GlyphKeyedDiffTest, CreatePatch_Glyf_ShortLoca) {
  const uint8_t header[] = {
      'i',  'f',  'g',  'k',  0x00, 0x00, 0x00, 0x00,  // reserved
      0x00,                                            // flags
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,  // compat id
      0x00, 0x00, 0x02, 0x1F,  // max uncompressed length (543)
  };

  {
    GlyphKeyedDiff differ(roboto, CompatId(1, 2, 3, 4), {FontHelper::kGlyf});
    auto patch = differ.CreatePatch({37, 40, 73, 91});
    ASSERT_TRUE(patch.ok()) << patch.status();

    ASSERT_EQ(patch->str(0, 29), absl::string_view((const char*)header, 29));

    FontData empty;
    FontData compressed_stream(patch->str(29));
    FontData uncompressed_stream;
    auto status =
        unbrotli.Patch(empty, compressed_stream, &uncompressed_stream);
    ASSERT_TRUE(status.ok()) << status;
    ASSERT_EQ(uncompressed_stream.str(),
              absl::string_view((const char*)data_stream_u16_short_loca, 543));
  }

  {
    // gvar is not in the font and should be ignored.
    GlyphKeyedDiff differ(roboto, CompatId(1, 2, 3, 4),
                          {FontHelper::kGlyf, FontHelper::kGvar});
    auto patch = differ.CreatePatch({37, 40, 73, 91});
    ASSERT_TRUE(patch.ok()) << patch.status();

    ASSERT_EQ(patch->str(0, 29), absl::string_view((const char*)header, 29));

    FontData empty;
    FontData compressed_stream(patch->str(29));
    FontData uncompressed_stream;
    auto status =
        unbrotli.Patch(empty, compressed_stream, &uncompressed_stream);
    ASSERT_TRUE(status.ok()) << status;
    ASSERT_EQ(uncompressed_stream.str(),
              absl::string_view((const char*)data_stream_u16_short_loca, 543));
  }
}

TEST_F(GlyphKeyedDiffTest, CreatePatch_Cff) {
  const uint8_t header[29] = {
      'i',  'f',  'g',  'k',  0x00, 0x00, 0x00, 0x00,  // reserved
      0x00,                                            // flags
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,  // compat id
      0x00, 0x00, 0x00, 0x39,  // max uncompressed length (57 bytes)
  };

  GlyphKeyedDiff differ(noto_sans_jp_cff, CompatId(1, 2, 3, 4),
                        {FontHelper::kCFF});
  auto patch = differ.CreatePatch({2});
  ASSERT_TRUE(patch.ok()) << patch.status();

  ASSERT_EQ(patch->str(0, 29), absl::string_view((const char*)header, 29));

  FontData empty;
  FontData compressed_stream(patch->str(29));
  FontData uncompressed_stream;
  auto status =
     unbrotli.Patch(empty, compressed_stream, &uncompressed_stream);
   ASSERT_TRUE(status.ok()) << status;
   ASSERT_EQ(uncompressed_stream.str(),
           absl::string_view((const char*)data_stream_u16_cff, 57));
}

TEST_F(GlyphKeyedDiffTest, CreatePatch_Gvar) {
  const uint8_t data_stream_header[] = {
      0x00,
      0x00,
      0x00,
      0x02,  // glyphCount
      0x01,  // table count

      // glyphIds[4]
      0x00,
      0x01,  // gid 1
      0x00,
      0x03,  // gid 3

      // tables[1]
      'g',
      'v',
      'a',
      'r',
  };
  std::string data_stream((const char*)data_stream_header, 13);

  auto face = roboto_vf.face();
  auto g1_gvar = FontHelper::GvarData(face.get(), 1);
  auto g3_gvar = FontHelper::GvarData(face.get(), 3);

  uint32_t size = 25;
  FontHelper::WriteUInt32(size, data_stream);
  size += g1_gvar->size();
  FontHelper::WriteUInt32(size, data_stream);
  size += g3_gvar->size();
  FontHelper::WriteUInt32(size, data_stream);

  data_stream += *g1_gvar;
  data_stream += *g3_gvar;

  const uint8_t header[] = {
      'i',  'f',  'g',  'k',  0x00, 0x00, 0x00, 0x00,  // reserved
      0x00,                                            // flags
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,  // compat id
  };
  std::string header_data((const char*)header, 25);
  FontHelper::WriteUInt32(data_stream.size(), header_data);

  GlyphKeyedDiff differ(roboto_vf, CompatId(1, 2, 3, 4), {FontHelper::kGvar});
  auto patch = differ.CreatePatch({1, 3});
  ASSERT_TRUE(patch.ok()) << patch.status();

  ASSERT_EQ(patch->str(0, 29), absl::string_view(header_data));

  FontData empty;
  FontData compressed_stream(patch->str(29));
  FontData uncompressed_stream;
  auto status = unbrotli.Patch(empty, compressed_stream, &uncompressed_stream);
  ASSERT_TRUE(status.ok()) << status;
  ASSERT_EQ(uncompressed_stream.str(), data_stream);
}

TEST_F(GlyphKeyedDiffTest, CreatePatch_GlyfGvar) {
  const uint8_t data_stream_header[] = {
      0x00,
      0x00,
      0x00,
      0x02,  // glyphCount
      0x02,  // table count

      // glyphIds[2]
      0x00,
      0x01,  // gid 1
      0x00,
      0x03,  // gid 3

      // tables[2]
      'g',
      'l',
      'y',
      'f',
      'g',
      'v',
      'a',
      'r',
  };
  std::string data_stream((const char*)data_stream_header, 17);

  auto face = roboto_vf.face();
  auto g1_glyf = FontHelper::GlyfData(face.get(), 1);
  auto g3_glyf = FontHelper::GlyfData(face.get(), 3);
  auto g1_gvar = FontHelper::GvarData(face.get(), 1);
  auto g3_gvar = FontHelper::GvarData(face.get(), 3);

  uint32_t size = 17 + 4 * 5;
  FontHelper::WriteUInt32(size, data_stream);
  size += g1_glyf->size();
  FontHelper::WriteUInt32(size, data_stream);
  size += g3_glyf->size();
  FontHelper::WriteUInt32(size, data_stream);
  size += g1_gvar->size();
  FontHelper::WriteUInt32(size, data_stream);
  size += g3_gvar->size();
  FontHelper::WriteUInt32(size, data_stream);

  data_stream += *g1_glyf;
  data_stream += *g3_glyf;
  data_stream += *g1_gvar;
  data_stream += *g3_gvar;

  const uint8_t header[] = {
      'i',  'f',  'g',  'k',  0x00, 0x00, 0x00, 0x00,  // reserved
      0x00,                                            // flags
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,  // compat id
  };
  std::string header_data((const char*)header, 25);
  FontHelper::WriteUInt32(data_stream.size(), header_data);

  GlyphKeyedDiff differ(roboto_vf, CompatId(1, 2, 3, 4),
                        {FontHelper::kGlyf, FontHelper::kGvar});
  auto patch = differ.CreatePatch({1, 3});
  ASSERT_TRUE(patch.ok()) << patch.status();

  ASSERT_EQ(patch->str(0, 29), absl::string_view(header_data));

  FontData empty;
  FontData compressed_stream(patch->str(29));
  FontData uncompressed_stream;
  auto status = unbrotli.Patch(empty, compressed_stream, &uncompressed_stream);
  ASSERT_TRUE(status.ok()) << status;
  ASSERT_EQ(uncompressed_stream.str(), data_stream);
}

TEST_F(GlyphKeyedDiffTest, CreatePatch_Glyf_InvalidGid) {
  GlyphKeyedDiff differ(roboto, CompatId(1, 2, 3, 4), {FontHelper::kGlyf});
  auto patch =
      differ.CreatePatch({37, 40, 73, 91, 100});  // gid 100 is not in the font
  ASSERT_EQ(patch.status(),
            absl::NotFoundError("Entry 100 not found in offset table."));
}

TEST_F(GlyphKeyedDiffTest, CreatePatch_Glyf_InvalidTable) {
  GlyphKeyedDiff differ(roboto, CompatId(1, 2, 3, 4),
                        {FontHelper::kGlyf, HB_TAG('f', 'o', 'o', 'o')});
  auto patch = differ.CreatePatch({37, 40, 73, 91});
  ASSERT_EQ(patch.status(),
            absl::InvalidArgumentError(
                "Unsupported table type for glyph keyed diff."));
}

// TODO(garretrieger): more tests for glyph keyed patch creation:
// - long loca
// - overflow tests

}  // namespace ift
