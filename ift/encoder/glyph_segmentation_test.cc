#include "ift/encoder/glyph_segmentation.h"

#include <google/protobuf/text_format.h>

#include "common/font_data.h"
#include "common/int_set.h"
#include "gtest/gtest.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"

using common::CodepointSet;
using common::FontData;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_face;
using common::SegmentSet;
using google::protobuf::TextFormat;
using ift::freq::UnicodeFrequencies;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;

namespace ift::encoder {

class GlyphSegmentationTest : public ::testing::Test {
 protected:
  GlyphSegmentationTest()
      : roboto(make_hb_face(nullptr)),
        noto_nastaliq_urdu(make_hb_face(nullptr)) {
    roboto = from_file("common/testdata/Roboto-Regular.ttf");
    noto_nastaliq_urdu =
        from_file("common/testdata/NotoNastaliqUrdu.subset.ttf");
  }

  hb_face_unique_ptr from_file(const char* filename) {
    hb_blob_t* blob = hb_blob_create_from_file_or_fail(filename);
    if (!blob) {
      assert(false);
    }
    FontData result(blob);
    hb_blob_destroy(blob);
    return result.face();
  }

  hb_face_unique_ptr roboto;
  hb_face_unique_ptr noto_nastaliq_urdu;
};

TEST_F(GlyphSegmentationTest, SimpleSegmentation_ToConfigProto) {
  ClosureGlyphSegmenter segmenter;
  auto segmentation =
      segmenter.CodepointToGlyphSegments(roboto.get(), {'a'}, {{'b'}, {'c'}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  auto config = segmentation->ToSegmentationPlanProto();
  std::string config_string;
  TextFormat::PrintToString(config, &config_string);

  // initial font: { gid0, gid69 }
  // p0: { gid70 }
  // p1: { gid71 }
  // if (s0) then p0
  // if (s1) then p1
  ASSERT_EQ(config_string, R"(segments {
  key: 0
  value {
    codepoints {
      values: 98
    }
    features {
    }
  }
}
segments {
  key: 1
  value {
    codepoints {
      values: 99
    }
    features {
    }
  }
}
glyph_patches {
  key: 0
  value {
    values: 70
  }
}
glyph_patches {
  key: 1
  value {
    values: 71
  }
}
glyph_patch_conditions {
  required_segments {
    values: 0
  }
  activated_patch: 0
}
glyph_patch_conditions {
  required_segments {
    values: 1
  }
  activated_patch: 1
}
initial_codepoints {
  values: 97
}
initial_features {
}
)");
}

TEST_F(GlyphSegmentationTest, SimpleSegmentationWithFeatures_ToConfigProto) {
  ClosureGlyphSegmenter segmenter;

  SubsetDefinition smcp;
  smcp.feature_tags.insert(HB_TAG('s', 'm', 'c', 'p'));

  SubsetDefinition init{'a'};
  init.feature_tags.insert(HB_TAG('d', 'l', 'i', 'g'));

  auto segmentation = segmenter.CodepointToGlyphSegments(roboto.get(), init,
                                                         {
                                                             {'b'},
                                                             {'c'},
                                                             smcp,
                                                         });
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  auto config = segmentation->ToSegmentationPlanProto();
  std::string config_string;
  TextFormat::PrintToString(config, &config_string);

  // initial font: { gid0, gid69 }
  // p0: { gid70 }
  // p1: { gid71 }
  // p2: { gid563 }
  // p3: { gid562 }
  // p4: { gid561 }
  // if (s0) then p0
  // if (s1) then p1
  // if (s2) then p2
  // if (s0 AND s2) then p3
  // if (s1 AND s2) then p4
  ASSERT_EQ(config_string, R"(segments {
  key: 0
  value {
    codepoints {
      values: 98
    }
    features {
    }
  }
}
segments {
  key: 1
  value {
    codepoints {
      values: 99
    }
    features {
    }
  }
}
segments {
  key: 2
  value {
    codepoints {
    }
    features {
      values: "smcp"
    }
  }
}
glyph_patches {
  key: 0
  value {
    values: 70
  }
}
glyph_patches {
  key: 1
  value {
    values: 71
  }
}
glyph_patches {
  key: 2
  value {
    values: 563
  }
}
glyph_patches {
  key: 3
  value {
    values: 562
  }
}
glyph_patches {
  key: 4
  value {
    values: 561
  }
}
glyph_patch_conditions {
  required_segments {
    values: 0
  }
  activated_patch: 0
}
glyph_patch_conditions {
  required_segments {
    values: 1
  }
  activated_patch: 1
}
glyph_patch_conditions {
  required_segments {
    values: 2
  }
  activated_patch: 2
}
glyph_patch_conditions {
  required_segments {
    values: 0
  }
  required_segments {
    values: 2
  }
  activated_patch: 3
}
glyph_patch_conditions {
  required_segments {
    values: 1
  }
  required_segments {
    values: 2
  }
  activated_patch: 4
}
initial_codepoints {
  values: 97
}
initial_features {
  values: "dlig"
}
)");
}

TEST_F(GlyphSegmentationTest, MixedAndOr_ToConfigProto) {
  ClosureGlyphSegmenter segmenter;
  UnicodeFrequencies freq;
  auto segmentation = segmenter.CodepointToGlyphSegments(
      roboto.get(), {'a'}, {{'f', 0xc1}, {'i', 0x106}});
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  auto config = segmentation->ToSegmentationPlanProto();
  std::string config_string;
  TextFormat::PrintToString(config, &config_string);

  // initial font: { gid0, gid69 }
  // p0: { gid37, gid74, gid640 }
  // p1: { gid39, gid77, gid700 }
  // p2: { gid444, gid446 }
  // p3: { gid117 }
  // if (s0) then p0
  // if (s1) then p1
  // if ((s0 OR s1)) then p3
  // if (s0 AND s1) then p2
  ASSERT_EQ(config_string, R"(segments {
  key: 0
  value {
    codepoints {
      values: 102
      values: 193
    }
    features {
    }
  }
}
segments {
  key: 1
  value {
    codepoints {
      values: 105
      values: 262
    }
    features {
    }
  }
}
glyph_patches {
  key: 0
  value {
    values: 37
    values: 74
    values: 640
  }
}
glyph_patches {
  key: 1
  value {
    values: 39
    values: 77
    values: 700
  }
}
glyph_patches {
  key: 2
  value {
    values: 444
    values: 446
  }
}
glyph_patches {
  key: 3
  value {
    values: 117
  }
}
glyph_patch_conditions {
  required_segments {
    values: 0
  }
  activated_patch: 0
}
glyph_patch_conditions {
  required_segments {
    values: 1
  }
  activated_patch: 1
}
glyph_patch_conditions {
  required_segments {
    values: 0
    values: 1
  }
  activated_patch: 3
}
glyph_patch_conditions {
  required_segments {
    values: 0
  }
  required_segments {
    values: 1
  }
  activated_patch: 2
}
initial_codepoints {
  values: 97
}
initial_features {
}
)");
}

TEST_F(GlyphSegmentationTest, MergeBases_ToConfigProto) {
  // {e, f} is too smal, since no conditional patches exist it should merge with
  // the next available base which is {'j', 'k'}
  ClosureGlyphSegmenter segmenter;
  auto segmentation =
      segmenter.CodepointToGlyphSegments(roboto.get(), {},
                                         {
                                             {'a', 'b', 'd'},
                                             {'e', 'f'},
                                             {'j', 'k'},
                                             {'m', 'n', 'o', 'p'},
                                         },
                                         MergeStrategy::Heuristic(370));
  ASSERT_TRUE(segmentation.ok()) << segmentation.status();

  auto config = segmentation->ToSegmentationPlanProto();
  std::string config_string;
  TextFormat::PrintToString(config, &config_string);

  // initial font: { gid0 }
  // p0: { gid69, gid70, gid72 }
  // p1: { gid73, gid74, gid78, gid79 }
  // p2: { gid81, gid82, gid83, gid84 }
  // if (s0) then p0
  // if (s1) then p1
  // if (s3) then p2
  ASSERT_EQ(config_string, R"(segments {
  key: 0
  value {
    codepoints {
      values: 97
      values: 98
      values: 100
    }
    features {
    }
  }
}
segments {
  key: 1
  value {
    codepoints {
      values: 101
      values: 102
      values: 106
      values: 107
    }
    features {
    }
  }
}
segments {
  key: 3
  value {
    codepoints {
      values: 109
      values: 110
      values: 111
      values: 112
    }
    features {
    }
  }
}
glyph_patches {
  key: 0
  value {
    values: 69
    values: 70
    values: 72
  }
}
glyph_patches {
  key: 1
  value {
    values: 73
    values: 74
    values: 78
    values: 79
  }
}
glyph_patches {
  key: 2
  value {
    values: 81
    values: 82
    values: 83
    values: 84
  }
}
glyph_patch_conditions {
  required_segments {
    values: 0
  }
  activated_patch: 0
}
glyph_patch_conditions {
  required_segments {
    values: 1
  }
  activated_patch: 1
}
glyph_patch_conditions {
  required_segments {
    values: 3
  }
  activated_patch: 2
}
initial_codepoints {
}
initial_features {
}
)");
}

TEST_F(GlyphSegmentationTest, ActivationConditionOrdering) {
  auto a = ActivationCondition::exclusive_segment(4, 8);
  auto b = ActivationCondition::and_segments(SegmentSet{4}, 8);
  auto c = ActivationCondition::and_segments(SegmentSet{4}, 9);

  auto d = ActivationCondition::or_segments(SegmentSet{4, 5}, 9);
  auto e = ActivationCondition::or_segments(SegmentSet{4, 6}, 9);

  auto f = ActivationCondition::and_segments(SegmentSet{4, 5}, 9);
  auto g = ActivationCondition::and_segments(SegmentSet{4, 6}, 9);

  ASSERT_EQ(a, a);
  ASSERT_EQ(b, b);
  ASSERT_EQ(c, c);
  ASSERT_EQ(d, d);
  ASSERT_EQ(e, e);
  ASSERT_EQ(f, f);
  ASSERT_EQ(g, g);

  ASSERT_NE(a, b);
  ASSERT_NE(b, c);
  ASSERT_NE(d, e);
  ASSERT_NE(f, g);

  ASSERT_LT(a, b);
  ASSERT_LT(b, c);
  ASSERT_LT(c, d);
  ASSERT_LT(d, e);
  ASSERT_LT(e, f);
  ASSERT_LT(f, g);

  ASSERT_FALSE(b < a);
  ASSERT_FALSE(c < b);
  ASSERT_FALSE(d < c);
  ASSERT_FALSE(e < d);
  ASSERT_FALSE(f < e);
  ASSERT_FALSE(g < f);
}

// TODO(garretrieger): add test where or_set glyphs are moved back to unmapped
// due to found "additional conditions".

}  // namespace ift::encoder