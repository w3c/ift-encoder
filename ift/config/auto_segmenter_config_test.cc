#include "ift/config/auto_segmenter_config.h"

#include <google/protobuf/text_format.h>

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hb.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/test_font_loader.h"
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
using ::testing::Each;
using ::testing::Eq;
using ::testing::UnorderedElementsAre;

class AutoSegmenterConfigTest : public ::testing::Test {
 protected:
  AutoSegmenterConfigTest()
      : resolver(*ift::common::BazelDataFileResolver::CreateForTest()),
        face_(make_hb_face(nullptr)),
        cjk_face_(make_hb_face(nullptr)) {}

  std::shared_ptr<ift::common::DataFileResolver> resolver;

  void SetUp() override {
    auto loader = ift::common::TestFontLoader::Default().value();

    face_ = loader->LoadFace("ift/common/testdata/Roboto-Regular.ttf").value();

    cjk_face_ =
        loader->LoadFace("ift/common/testdata/NotoSansJP-Regular.ttf").value();
  }

  hb_face_unique_ptr face_;
  hb_face_unique_ptr cjk_face_;
};

// A merge group: it's name and the list of frequency data sets it uses.
using ScriptGroup = std::pair<std::string, std::vector<std::string>>;

static std::vector<ScriptGroup> GetScripts(const SegmenterConfig& config) {
  std::vector<ScriptGroup> result;
  for (const auto& mg : config.merge_groups()) {
    std::vector<std::string> freq_data;
    for (const auto& config : mg.cost_config().frequency_data()) {
      freq_data.push_back(config.built_in_freq_data_name());
    }
    result.push_back({mg.name(), freq_data});
  }
  return result;
}

// Returns the names of the frequency data sets which have an initial font
// merge threshold configured.
static std::vector<std::string> GetScriptsWithInitialMergeThreshold(
    const SegmenterConfig& config) {
  std::vector<std::string> result;
  for (const auto& mg : config.merge_groups()) {
    for (const auto& config : mg.cost_config().frequency_data()) {
      if (config.has_initial_font_merge_threshold()) {
        result.push_back(config.built_in_freq_data_name());
      }
    }
  }
  return result;
}

const ScriptGroup kCyrillic = {"Cyrillic", {"Script_cyrillic.riegeli"}};
const ScriptGroup kGreek = {"Greek", {"Script_greek.riegeli"}};
const ScriptGroup kSymbols = {"Symbols", {"Script_symbols.riegeli"}};
const ScriptGroup kLatin = {"Latin", {"Script_latin.riegeli"}};
const ScriptGroup kLanguageFr = {"Language_fr", {"Language_fr.riegeli"}};
const ScriptGroup kFallback = {"Fallback", {"fallback.riegeli"}};
// Most of the emoji code points in the font are ones shared with latin
// (digits, #, *, (c), (R), TM) so emoji is grouped with latin. Emoji also
// shares enough of its probability mass with symbols to pull symbols into the
// same group.
const ScriptGroup kEmojiLatinAndSymbols = {
    "Emoji+Latin+Symbols",
    {"Script_emoji.riegeli", "Script_latin.riegeli", "Script_symbols.riegeli"}};
const ScriptGroup kCJK = {
    "Chinese-simplified+Chinese-traditional+Japanese+Korean",
    {"Script_chinese-simplified.riegeli@*",
     "Script_chinese-traditional.riegeli@*", "Script_japanese.riegeli@*",
     "Script_korean.riegeli@*"}};
const ScriptGroup kZhHansCJK = {
    "Language_zh-Hans+Chinese-traditional+Japanese+Korean",
    {"Language_zh-Hans.riegeli@*", "Script_chinese-traditional.riegeli@*",
     "Script_japanese.riegeli@*", "Script_korean.riegeli@*"}};
const ScriptGroup kUnifiedCJK = {"CJK", {"Script_CJK.riegeli@*"}};

TEST_F(AutoSegmenterConfigTest, Roboto_UnspecifiedPrimary) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kSymbols, kCyrillic, kGreek, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_latin.riegeli"));

  std::string config_string;
  TextFormat::PrintToString(*config_or, &config_string);
  ASSERT_EQ(config_string, R"(unmapped_glyph_handling: MOVE_TO_INIT_FONT
generate_table_keyed_segments: true
brotli_quality: 11
brotli_quality_for_initial_font_merging: 9
base_heuristic_config {
  min_patch_size: 2500
}
base_cost_config {
  network_overhead_cost: 200
  min_group_size: 4
  optimization_cutoff_fraction: 0.005
  experimental_use_patch_merges: true
}
ungrouped_config {
  min_patch_size: 2500
}
preprocess_merging_group_size_for_ungrouped: 12
merge_groups {
  name: "Cyrillic"
  preprocess_merging_group_size: 4
  preprocess_merging_probability_threshold: 0.005
  cost_config {
    frequency_data {
      built_in_freq_data_name: "Script_cyrillic.riegeli"
      use_bigrams: true
    }
  }
}
merge_groups {
  name: "Greek"
  preprocess_merging_group_size: 4
  preprocess_merging_probability_threshold: 0.005
  cost_config {
    frequency_data {
      built_in_freq_data_name: "Script_greek.riegeli"
      use_bigrams: true
    }
  }
}
merge_groups {
  name: "Latin"
  preprocess_merging_group_size: 4
  preprocess_merging_probability_threshold: 0.005
  cost_config {
    frequency_data {
      built_in_freq_data_name: "Script_latin.riegeli"
      use_bigrams: true
      initial_font_merge_threshold: -160
      initial_font_merge_probability_threshold: 0.3
    }
  }
}
merge_groups {
  name: "Symbols"
  preprocess_merging_group_size: 4
  preprocess_merging_probability_threshold: 0.005
  cost_config {
    frequency_data {
      built_in_freq_data_name: "Script_symbols.riegeli"
      use_bigrams: true
    }
  }
}
merge_groups {
  name: "Fallback"
  preprocess_merging_group_size: 4
  preprocess_merging_probability_threshold: 0.005
  cost_config {
    frequency_data {
      built_in_freq_data_name: "fallback.riegeli"
      use_bigrams: true
    }
  }
}
base_segmentation_plan {
  jump_ahead: 2
  use_prefetch_lists: true
}
generate_feature_segments: true
)"
                           "condition_analysis_mode: DEP_GRAPH_ONLY\n"
  );
}

TEST_F(AutoSegmenterConfigTest, Roboto_ScriptCyrillic) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver,
                                                       "Script_cyrillic");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kSymbols, kCyrillic, kGreek, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_cyrillic.riegeli"));
}

TEST_F(AutoSegmenterConfigTest, Roboto_LanguageFr) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver,
                                                       "Language_fr");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kLanguageFr, kSymbols, kCyrillic, kGreek,
                                   kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Language_fr.riegeli"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_UnspecifiedPrimary) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(cjk_face_.get(), *resolver);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  // The individual CJK scripts heavily overlap each other so they are
  // combined into a single merge group.
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kEmojiLatinAndSymbols, kGreek, kCyrillic,
                                   kCJK, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_latin.riegeli"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_ScriptCJK) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or = AutoSegmenterConfig::GenerateConfig(cjk_face_.get(),
                                                       *resolver, "Script_CJK");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  // The unified CJK data set replaces all of the individual CJK scripts.
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kEmojiLatinAndSymbols, kGreek, kCyrillic,
                                   kUnifiedCJK, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_CJK.riegeli@*"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_ScriptJapanese) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or = AutoSegmenterConfig::GenerateConfig(
      cjk_face_.get(), *resolver, "Script_japanese");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  // Japanese remains grouped with the other CJK scripts, but only it gets
  // the initial font merge threshold.
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kEmojiLatinAndSymbols, kGreek, kCyrillic,
                                   kCJK, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_japanese.riegeli@*"));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_LanguageZhHans) {
  if (!cjk_face_) GTEST_SKIP() << "NotoSansJP-Regular.ttf not found";
  auto config_or = AutoSegmenterConfig::GenerateConfig(
      cjk_face_.get(), *resolver, "Language_zh-Hans");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kEmojiLatinAndSymbols, kGreek, kCyrillic,
                                   kZhHansCJK, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Language_zh-Hans.riegeli@*"));
}

TEST_F(AutoSegmenterConfigTest, Roboto_ScriptNotFound) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver,
                                                       "Script_foobar");
  EXPECT_EQ(config_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(AutoSegmenterConfigTest, Roboto_LanguageNotFound) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver,
                                                       "Language_foobar");
  EXPECT_EQ(config_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(AutoSegmenterConfigTest, Roboto_InvalidPrefix) {
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver, "Foo_latin");
  EXPECT_EQ(config_or.status().code(), absl::StatusCode::kInternal);
}

TEST_F(AutoSegmenterConfigTest, Roboto_FullFileName_Script) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(
      face_.get(), *resolver, "Script_cyrillic.riegeli");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kSymbols, kCyrillic, kGreek, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_cyrillic.riegeli"));
}

TEST_F(AutoSegmenterConfigTest, Roboto_FullFileName_Language) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver,
                                                       "Language_fr.riegeli");
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kLanguageFr, kSymbols, kCyrillic, kGreek,
                                   kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Language_fr.riegeli"));
}

TEST_F(AutoSegmenterConfigTest, LanguageMappingsExist) {
  auto built_in_freqs_or = ift::config::BuiltInFrequenciesList(*resolver);
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

// Returns the use_bigrams setting of every frequency data set in the config.
static std::vector<bool> GetUseBigrams(const SegmenterConfig& config) {
  std::vector<bool> result;
  for (const auto& mg : config.merge_groups()) {
    for (const auto& freq_config : mg.cost_config().frequency_data()) {
      result.push_back(freq_config.use_bigrams());
    }
  }
  return result;
}

TEST_F(AutoSegmenterConfigTest, QualityLevelForcing) {
  auto config_or = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver,
                                                       std::nullopt, 1);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_EQ(config_or->brotli_quality(), 0);
  EXPECT_EQ(config_or->unmapped_glyph_handling(), MOVE_TO_INIT_FONT);
  EXPECT_THAT(GetUseBigrams(*config_or), Each(false));
  EXPECT_EQ(config_or->brotli_quality_for_initial_font_merging(), 0);
  EXPECT_EQ(config_or->base_cost_config().optimization_cutoff_fraction(), 0.05);

  auto config_or_8 = AutoSegmenterConfig::GenerateConfig(face_.get(), *resolver,
                                                         std::nullopt, 7);
  ASSERT_TRUE(config_or_8.ok()) << config_or_8.status();
  EXPECT_EQ(config_or_8->brotli_quality(), 11);
  EXPECT_EQ(config_or_8->unmapped_glyph_handling(), MOVE_TO_INIT_FONT);
  EXPECT_THAT(GetUseBigrams(*config_or_8), Each(true));
  EXPECT_EQ(config_or_8->brotli_quality_for_initial_font_merging(), 11);
  EXPECT_EQ(config_or_8->base_cost_config().optimization_cutoff_fraction(),
            0.005);
}

}  // namespace
}  // namespace ift::config
