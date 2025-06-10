#include "ift/url_template.h"

#include "gtest/gtest.h"

namespace ift {

class URLTemplateTest : public ::testing::Test {
 protected:
  URLTemplateTest() {}
};

TEST_F(URLTemplateTest, PatchToUrl_NoFormatters) {
  const std::vector<uint8_t> no_variables{10,  '/', '/', 'f', 'o', 'o',
                                          '.', 'b', 'a', 'r', '/'};
  EXPECT_EQ(*URLTemplate::PatchToUrl(no_variables, 0), "//foo.bar/");
  EXPECT_EQ(*URLTemplate::PatchToUrl(no_variables, 5), "//foo.bar/");
}

TEST_F(URLTemplateTest, PatchToUrl_Basic) {
  // Test cases from: https://w3c.github.io/IFT/Overview.html#url-templates
  const std::vector<uint8_t> just_id{10,  '/', '/', 'f', 'o', 'o',
                                     '.', 'b', 'a', 'r', '/', 128};
  const std::vector<uint8_t> d1_d2_id{10,  '/', '/', 'f', 'o', 'o',
                                      '.', 'b', 'a', 'r', '/', 129,
                                      1,   '/', 130, 1,   '/', 128};
  const std::vector<uint8_t> d1_d2_d3_id{10,  '/', '/', 'f', 'o', 'o', '.',
                                         'b', 'a', 'r', '/', 129, 1,   '/',
                                         130, 1,   '/', 131, 1,   '/', 128};

  EXPECT_EQ(*URLTemplate::PatchToUrl(just_id, 0), "//foo.bar/00");
  EXPECT_EQ(*URLTemplate::PatchToUrl(just_id, 123), "//foo.bar/FC");
  EXPECT_EQ(*URLTemplate::PatchToUrl(d1_d2_id, 478), "//foo.bar/0/F/07F0");
  EXPECT_EQ(*URLTemplate::PatchToUrl(d1_d2_d3_id, 123), "//foo.bar/C/F/_/FC");
}

TEST_F(URLTemplateTest, InvalidTemplates) {
  const std::vector<uint8_t> bad_opcode{10,  '/', '/', 'f', 'o', 'o',
                                        '.', 'b', 'a', 'r', '/', 134};
  EXPECT_TRUE(
      absl::IsInvalidArgument(URLTemplate::PatchToUrl(bad_opcode, 0).status()));

  const std::vector<uint8_t> insert_0{0,   10,  '/', '/', 'f', 'o',
                                      'o', '.', 'b', 'a', 'r', '/'};
  EXPECT_TRUE(
      absl::IsInvalidArgument(URLTemplate::PatchToUrl(insert_0, 0).status()));

  const std::vector<uint8_t> insert_eof{9,   '/', '/', 'f', 'o', 'o',
                                        '.', 'b', 'a', 'r', '/'};
  EXPECT_TRUE(
      absl::IsInvalidArgument(URLTemplate::PatchToUrl(insert_eof, 0).status()));
}

}  // namespace ift
