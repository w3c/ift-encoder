#include "ift/config/auto_segmenter_config.h"

#include <google/protobuf/text_format.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hb.h"
#include "ift/common/font_data.h"
#include "ift/config/load_codepoints.h"

using ift::config::CLOSURE_AND_DEP_GRAPH;
using ift::config::FIND_CONDITIONS;
using ift::config::MOVE_TO_INIT_FONT;
using ift::config::SegmenterConfig;

namespace ift::config {
namespace {

using google::protobuf::TextFormat;
using ::ift::common::hb_blob_unique_ptr;
using ::ift::common::hb_face_unique_ptr;
using ::ift::common::make_hb_blob;
using ::ift::common::make_hb_face;
using ::testing::Eq;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

class AutoSegmenterConfigTest : public ::testing::Test {
 protected:
  AutoSegmenterConfigTest()
      : face_(make_hb_face(nullptr)), cjk_face_(make_hb_face(nullptr)) {}

  void SetUp() override {
    hb_blob_unique_ptr roboto_blob = make_hb_blob(
        hb_blob_create_from_file("ift/common/testdata/Roboto-Regular.ttf"));
    face_ = make_hb_face(hb_face_create(roboto_blob.get(), 0));

    hb_blob_unique_ptr noto_blob = make_hb_blob(
        hb_blob_create_from_file("ift/common/testdata/NotoSansJP-Regular.ttf"));
    if (hb_blob_get_length(noto_blob.get()) > 0) {
      cjk_face_ = make_hb_face(hb_face_create(noto_blob.get(), 0));
    }
  }

  hb_face_unique_ptr face_;
  hb_face_unique_ptr cjk_face_;
};

using ScriptPair = std::pair<std::string, std::string>;

static std::vector<ScriptPair> GetScripts(const SegmenterConfig& config) {
  std::vector<ScriptPair> result;
  for (const auto& mg : config.merge_groups()) {
    result.push_back({mg.name(), mg.cost_config().built_in_freq_data_name()});
  }
  return result;
}

static std::vector<std::string> GetScriptsWithInitialMergeThreshold(
    const SegmenterConfig& config) {
  std::vector<std::string> result;
  for (const auto& mg : config.merge_groups()) {
    if (mg.cost_config().has_initial_font_merge_threshold()) {
      result.push_back(mg.name());
    }
  }
  return result;
}

const ScriptPair kLatin = {"Latin", "Script_latin.riegeli"};
const ScriptPair kCyrillic = {"Cyrillic", "Script_cyrillic.riegeli"};
const ScriptPair kGreek = {"Greek", "Script_greek.riegeli"};
const ScriptPair kSymbols = {"Symbols", "Script_symbols.riegeli"};
const ScriptPair kEmoji = {"Emoji", "Script_emoji.riegeli"};
const ScriptPair kCJK = {"CJK", "Script_CJK.riegeli@*"};
const ScriptPair kFallback = {"Fallback", "fallback.riegeli"};

TEST_F(AutoSegmenterConfigTest, Roboto_UnspecifiedPrimary) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get());
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kCyrillic, kGreek, kSymbols, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Latin"));

  std::string config_string;
  TextFormat::PrintToString(*config_or, &config_string);
  ASSERT_EQ(config_string, R"(unmapped_glyph_handling: MOVE_TO_INIT_FONT
generate_table_keyed_segments: true
brotli_quality: 11
brotli_quality_for_initial_font_merging: 11
base_heuristic_config {
  min_patch_size: 2500
}
base_cost_config {
  use_bigrams: true
  min_group_size: 4
  optimization_cutoff_fraction: 0.005
}
ungrouped_config {
  min_patch_size: 2500
}
preprocess_merging_group_size_for_ungrouped: 12
merge_groups {
  name: "Cyrillic"
  preprocess_merging_group_size: 1
  cost_config {
    built_in_freq_data_name: "Script_cyrillic.riegeli"
  }
}
merge_groups {
  name: "Greek"
  preprocess_merging_group_size: 1
  cost_config {
    built_in_freq_data_name: "Script_greek.riegeli"
  }
}
merge_groups {
  name: "Latin"
  preprocess_merging_group_size: 1
  cost_config {
    built_in_freq_data_name: "Script_latin.riegeli"
    initial_font_merge_threshold: -60
    initial_font_merge_probability_threshold: 0.25
  }
}
merge_groups {
  name: "Symbols"
  preprocess_merging_group_size: 1
  cost_config {
    built_in_freq_data_name: "Script_symbols.riegeli"
  }
}
merge_groups {
  name: "Fallback"
  preprocess_merging_group_size: 1
  cost_config {
    built_in_freq_data_name: "fallback.riegeli"
  }
}
base_segmentation_plan {
  jump_ahead: 2
  use_prefetch_lists: true
}
generate_feature_segments: true
condition_analysis_mode: DEP_GRAPH_ONLY
)");
}

TEST_F(AutoSegmenterConfigTest, Roboto_ScriptCyrillic) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), "Script_cyrillic");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kCyrillic, kGreek, kSymbols, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Cyrillic"));
}

TEST_F(AutoSegmenterConfigTest, Roboto_LanguageFr) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), "Language_fr");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(Pair("Language_fr", "Language_fr.riegeli"),
                                   kCyrillic, kGreek, kSymbols, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Language_fr"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_UnspecifiedPrimary) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or = AutoSegmenterConfig::GenerateConfig(cjk_face_.get());
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kLatin, kGreek, kCyrillic, kCJK, kSymbols,
                                   kEmoji, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Latin"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_ScriptCJK) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(cjk_face_.get(), "Script_CJK");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kLatin, kGreek, kCyrillic, kCJK, kSymbols,
                                   kEmoji, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("CJK"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_ScriptJapanese) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(cjk_face_.get(), "Script_japanese");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kGreek, kCyrillic,
                           Pair("Japanese", "Script_japanese.riegeli@*"),
                           kSymbols, kEmoji, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Japanese"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_LanguageZhHans) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(cjk_face_.get(), "Language_zh-Hans");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(
                  kLatin, kGreek, kCyrillic,
                  Pair("Language_zh-Hans", "Language_zh-Hans.riegeli@*"),
                  kSymbols, kEmoji, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Language_zh-Hans"));
}

TEST_F(AutoSegmenterConfigTest, Roboto_ScriptNotFound) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), "Script_foobar");
  EXPECT_EQ(config_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(AutoSegmenterConfigTest, Roboto_LanguageNotFound) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), "Language_foobar");
  EXPECT_EQ(config_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(AutoSegmenterConfigTest, Roboto_InvalidPrefix) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), "Foo_latin");
  EXPECT_EQ(config_or.status().code(), absl::StatusCode::kInternal);
}

TEST_F(AutoSegmenterConfigTest, Roboto_FullFileName_Script) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(
      face_.get(), "Script_cyrillic.riegeli");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kCyrillic, kGreek, kSymbols, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Cyrillic"));
}

TEST_F(AutoSegmenterConfigTest, Roboto_FullFileName_Language) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), "Language_fr.riegeli");
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(Pair("Language_fr", "Language_fr.riegeli"),
                                   kCyrillic, kGreek, kSymbols, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Language_fr"));
}

TEST_F(AutoSegmenterConfigTest, LanguageMappingsExist) {
  auto built_in_freqs_or = ift::config::BuiltInFrequenciesList();
  ASSERT_TRUE(built_in_freqs_or.ok());
  for (const auto& [file_name, _] : *built_in_freqs_or) {
    if (!absl::StartsWith(file_name, "Language_")) continue;
    std::string language = file_name;
    size_t dot_pos = language.find('.');
    if (dot_pos != std::string::npos) language = language.substr(0, dot_pos);
    auto base_script = AutoSegmenterConfig::GetBaseScriptForLanguage(language);
    ASSERT_TRUE(base_script.ok())
        << "No mapping for " << language << ": " << base_script.status();
  }
}

TEST_F(AutoSegmenterConfigTest, QualityLevelForcing) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), std::nullopt, 1);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_EQ(config_or->brotli_quality(), 0);
  EXPECT_EQ(config_or->unmapped_glyph_handling(), MOVE_TO_INIT_FONT);
  EXPECT_EQ(config_or->base_cost_config().use_bigrams(), false);
  EXPECT_EQ(config_or->brotli_quality_for_initial_font_merging(), 0);
  EXPECT_EQ(config_or->base_cost_config().optimization_cutoff_fraction(), 0.05);

  auto config_or_8 =
      AutoSegmenterConfig::GenerateConfig(face_.get(), std::nullopt, 8);
  ASSERT_TRUE(config_or_8.ok()) << config_or_8.status();
  EXPECT_EQ(config_or_8->brotli_quality(), 11);
  EXPECT_EQ(config_or_8->unmapped_glyph_handling(), MOVE_TO_INIT_FONT);
  EXPECT_EQ(config_or_8->base_cost_config().use_bigrams(), true);
  EXPECT_EQ(config_or_8->brotli_quality_for_initial_font_merging(), 11);
  EXPECT_EQ(config_or_8->base_cost_config().optimization_cutoff_fraction(),
            0.005);
}

}  // namespace
}  // namespace ift::config
