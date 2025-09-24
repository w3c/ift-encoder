#include "common/woff2.h"

#include "gtest/gtest.h"

using absl::Span;
using absl::string_view;

namespace common {

class Woff2Test : public ::testing::Test {
 protected:
  Woff2Test() {
    font = from_file("common/testdata/Roboto-Regular.abcd.ttf");
    woff2_font = from_file("common/testdata/Roboto-Regular.abcd.woff2");
  }

  FontData from_file(const char* filename) {
    hb_blob_t* blob = hb_blob_create_from_file(filename);
    FontData result(blob);
    hb_blob_destroy(blob);
    return result;
  }

  FontData font;
  FontData woff2_font;
};

TEST_F(Woff2Test, EncodeWoff2) {
  auto woff2 = Woff2::EncodeWoff2(font.str());
  ASSERT_TRUE(woff2.ok()) << woff2.status();

  ASSERT_GT(woff2->size(), 48);
  ASSERT_EQ("wOF2", string_view(woff2->data(), 4));
  ASSERT_LT(woff2->size(), font.size());
}

TEST_F(Woff2Test, EncodeWoff2_Quality) {
  auto woff2_high = Woff2::EncodeWoff2(font.str(), true, 11);
  auto woff2_low = Woff2::EncodeWoff2(font.str(), true, 6);
  ASSERT_TRUE(woff2_high.ok()) << woff2_high.status();
  ASSERT_TRUE(woff2_low.ok()) << woff2_low.status();
  ASSERT_LT(woff2_high->size(), woff2_low->size());
}

TEST_F(Woff2Test, EncodeWoff2_NoGlyfTransform) {
  auto woff2 = Woff2::EncodeWoff2(font.str(), true);
  auto woff2_no_transform = Woff2::EncodeWoff2(font.str(), false);
  ASSERT_TRUE(woff2.ok()) << woff2.status();
  ASSERT_TRUE(woff2_no_transform.ok()) << woff2_no_transform.status();

  ASSERT_GT(woff2->size(), 48);
  ASSERT_GT(woff2_no_transform->size(), 48);
  ASSERT_EQ("wOF2", string_view(woff2->data(), 4));
  ASSERT_EQ("wOF2", string_view(woff2_no_transform->data(), 4));
  ASSERT_LT(woff2->size(), font.size());
  ASSERT_LT(woff2_no_transform->size(), font.size());
  ASSERT_NE(woff2_no_transform->size(), woff2->size());
}

TEST_F(Woff2Test, EncodeWoff2_Fails) {
  auto woff2 = Woff2::EncodeWoff2(woff2_font.str());
  ASSERT_TRUE(absl::IsInternal(woff2.status())) << woff2.status();
}

TEST_F(Woff2Test, DecodeWoff2) {
  auto font = Woff2::DecodeWoff2(woff2_font.str());
  ASSERT_TRUE(font.ok()) << font.status();

  ASSERT_GT(font->size(), woff2_font.size());
  uint8_t true_type_tag[] = {0, 1, 0, 0};
  ASSERT_EQ(true_type_tag,
            Span<const uint8_t>((const uint8_t*)font->data(), 4));
}

TEST_F(Woff2Test, DecodeWoff2_Fails) {
  auto ttf = Woff2::DecodeWoff2(font.str());
  ASSERT_TRUE(absl::IsInternal(ttf.status())) << ttf.status();
}

}  // namespace common