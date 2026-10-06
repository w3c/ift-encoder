#include "ift/config/auto_segmenter_config.h"

#include <google/protobuf/text_format.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "hb-subset.h"
#include "hb.h"
#include "ift/common/bazel_data_file_resolver.h"
#include "ift/common/font_data.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
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
using ::ift::common::CodepointSet;
using ::ift::common::FontHelper;
using ::testing::AnyOfArray;
using ::testing::Contains;
using ::testing::Each;
using ::testing::Eq;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

class AutoSegmenterConfigTest : public ::testing::Test,
                                protected AutoSegmenterConfig {
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

  // Returns face reduced down to only the supplied codepoints.
  static hb_face_unique_ptr Subset(
      hb_face_t* face, const ift::common::CodepointSet& codepoints) {
    hb_subset_input_t* input = hb_subset_input_create_or_fail();
    for (hb_codepoint_t cp : codepoints) {
      hb_set_add(hb_subset_input_unicode_set(input), cp);
    }

    hb_face_unique_ptr subset = make_hb_face(hb_subset_or_fail(face, input));
    hb_subset_input_destroy(input);
    return subset;
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

// Returns the set of codepoints that the merge group named group_name is
// explicitly configured to cover. Returns nullopt if there's no such group or
// if it doesn't have an explicit coverage set (in which case coverage is
// derived from the groups frequency data).
static std::optional<ift::common::CodepointSet> GetSegmentIds(
    const SegmenterConfig& config, absl::string_view group_name) {
  for (const auto& mg : config.merge_groups()) {
    if (mg.name() != group_name || !mg.has_segment_ids()) {
      continue;
    }

    ift::common::CodepointSet result;
    for (uint32_t value : mg.segment_ids().values()) {
      result.insert(value);
    }
    return result;
  }
  return std::nullopt;
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
const ScriptGroup kJapanese = {"Japanese", {"Script_japanese.riegeli@*"}};
// The CJK scripts left over once an explicitly specified japanese primary
// script has been split out into it's own group.
const ScriptGroup kCJKWithoutJapanese = {
    "Chinese-simplified+Chinese-traditional+Korean",
    {"Script_chinese-simplified.riegeli@*",
     "Script_chinese-traditional.riegeli@*", "Script_korean.riegeli@*"}};
const ScriptGroup kLanguageZhHans = {"Language_zh-Hans",
                                     {"Language_zh-Hans.riegeli@*"}};
const ScriptGroup kCJKWithoutZhHans = {
    "Chinese-traditional+Japanese+Korean",
    {"Script_chinese-traditional.riegeli@*", "Script_japanese.riegeli@*",
     "Script_korean.riegeli@*"}};
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
brotli_quality: 11
brotli_quality_for_initial_font_merging: 9
base_heuristic_config {
  min_patch_size: 2500
  max_patch_size: 20000
}
base_cost_config {
  network_overhead_cost: 200
  min_group_size: 4
  optimization_cutoff_fraction: 0.005
  experimental_use_patch_merges: true
}
ungrouped_config {
  min_patch_size: 2500
  max_patch_size: 20000
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
condition_analysis_mode: DEP_GRAPH_ONLY
generate_table_keyed_segments_mode: FROM_FREQ_DATA
)");
}

TEST_F(AutoSegmenterConfigTest, EstimateTableKeyedPatchCount) {
  ASSERT_EQ(AutoSegmenterConfig::EstimateTableKeyedPatchCount(3, 3), 12);
  ASSERT_EQ(AutoSegmenterConfig::EstimateTableKeyedPatchCount(3, 2), 6);
  ASSERT_EQ(AutoSegmenterConfig::EstimateTableKeyedPatchCount(3, 1), 1);

  ASSERT_EQ(AutoSegmenterConfig::EstimateTableKeyedPatchCount(4, 4), 32);
  ASSERT_EQ(AutoSegmenterConfig::EstimateTableKeyedPatchCount(4, 3), 22);
  ASSERT_EQ(AutoSegmenterConfig::EstimateTableKeyedPatchCount(4, 2), 8);
  ASSERT_EQ(AutoSegmenterConfig::EstimateTableKeyedPatchCount(4, 1), 1);
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
  ASSERT_TRUE(cjk_face_) << "NotoSansJP-Regular.ttf not found";

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
  EXPECT_EQ(config_or->base_segmentation_plan().max_depth(), 4);
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_ScriptCJK) {
  ASSERT_TRUE(cjk_face_) << "NotoSansJP-Regular.ttf not found";

  auto config_or = AutoSegmenterConfig::GenerateConfig(cjk_face_.get(),
                                                       *resolver, "Script_CJK");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  // The unified CJK data set replaces all of the individual CJK scripts.
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kEmojiLatinAndSymbols, kGreek, kCyrillic,
                                   kUnifiedCJK, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_CJK.riegeli@*"));
  EXPECT_EQ(config_or->base_segmentation_plan().max_depth(), 6);
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_ScriptJapanese) {
  ASSERT_TRUE(cjk_face_) << "NotoSansJP-Regular.ttf not found";

  auto config_or = AutoSegmenterConfig::GenerateConfig(
      cjk_face_.get(), *resolver, "Script_japanese");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  // Japanese has been explicitly selected as the primary script so it's given
  // a merge group of it's own (which biases merging towards optimizing
  // japanese). The remaining CJK scripts get a second group.
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kEmojiLatinAndSymbols, kGreek, kCyrillic,
                                   kJapanese, kCJKWithoutJapanese, kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_japanese.riegeli@*"));

  // The primary script's group covers everything it's frequency data covers,
  // so it doesn't need an explicit coverage.
  EXPECT_FALSE(GetSegmentIds(*config_or, kJapanese.first).has_value());

  auto freq_list = BuiltInFrequenciesList(*resolver);
  ASSERT_TRUE(freq_list.ok()) << freq_list.status();

  // The second group is configured to cover only the codepoints which are
  // not covered by the japanese group.
  auto covered = GetSegmentIds(*config_or, kCJKWithoutJapanese.first);
  ASSERT_TRUE(covered.has_value());
  EXPECT_FALSE(covered->empty());
  EXPECT_FALSE(
      covered->intersects(freq_list->at("Script_japanese.riegeli@*")));

  CodepointSet font_unicodes = FontHelper::ToCodepointsSet(cjk_face_.get());
  CodepointSet expected;
  for (const auto& script : kCJKWithoutJapanese.second) {
    CodepointSet remaining_script_codepoints = freq_list->at(script);
    remaining_script_codepoints.intersect(font_unicodes);
    remaining_script_codepoints.subtract(freq_list->at("Script_japanese.riegeli@*"));
    ASSERT_TRUE(remaining_script_codepoints.is_subset_of(*covered));
    expected.union_set(remaining_script_codepoints);
  }
  ASSERT_EQ(*covered, expected);
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_LanguageZhHans) {
  ASSERT_TRUE(cjk_face_) << "NotoSansJP-Regular.ttf not found";

  auto config_or = AutoSegmenterConfig::GenerateConfig(
      cjk_face_.get(), *resolver, "Language_zh-Hans");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or),
              UnorderedElementsAre(kEmojiLatinAndSymbols, kGreek, kCyrillic,
                                   kLanguageZhHans, kCJKWithoutZhHans,
                                   kFallback));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Language_zh-Hans.riegeli@*"));

  auto freq_list = BuiltInFrequenciesList(*resolver);
  ASSERT_TRUE(freq_list.ok()) << freq_list.status();

  EXPECT_FALSE(GetSegmentIds(*config_or, kLanguageZhHans.first).has_value());

  auto covered = GetSegmentIds(*config_or, kCJKWithoutZhHans.first);
  ASSERT_TRUE(covered.has_value());
  EXPECT_FALSE(covered->empty());
  EXPECT_FALSE(
      covered->intersects(freq_list->at("Language_zh-Hans.riegeli@*")));
}

TEST_F(AutoSegmenterConfigTest, NotoSansJP_PrimaryScriptCoversWholeGroup) {
  ASSERT_TRUE(cjk_face_) << "NotoSansJP-Regular.ttf not found";

  auto freq_list = BuiltInFrequenciesList(*resolver);
  ASSERT_TRUE(freq_list.ok()) << freq_list.status();

  // Reduce the font down to only the codepoints which the japanese frequency
  // data covers. Emoji is excluded as well, otherwise the handful of emoji
  // codepoints that remain would be pulled into the CJK merge group.
  CodepointSet subset_codepoints = freq_list->at("Script_japanese.riegeli@*");
  subset_codepoints.subtract(freq_list->at("Script_emoji.riegeli"));
  hb_face_unique_ptr subset = Subset(cjk_face_.get(), subset_codepoints);
  ASSERT_TRUE(subset.get());

  // The other CJK scripts are still present in the subsetted font and are
  // still grouped together with japanese.
  auto config_or =
      AutoSegmenterConfig::GenerateConfig(subset.get(), *resolver);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or), Contains(kCJK));

  // However japanese covers all of the codepoints in the font, so once it's
  // split out into it's own group there's nothing left for the other CJK
  // scripts to cover and no group is created for them.
  config_or = AutoSegmenterConfig::GenerateConfig(subset.get(), *resolver,
                                                  "Script_japanese");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or), Contains(kJapanese));
  EXPECT_THAT(GetScripts(*config_or), Not(Contains(kCJKWithoutJapanese)));
  for (const auto& [name, data_sets] : GetScripts(*config_or)) {
    EXPECT_THAT(data_sets, Each(Not(AnyOfArray(kCJKWithoutJapanese.second))))
        << "in merge group " << name;
  }
}

TEST_F(AutoSegmenterConfigTest, PrimaryScriptNotInFont) {
  auto freq_list = BuiltInFrequenciesList(*resolver);
  ASSERT_TRUE(freq_list.ok()) << freq_list.status();

  // Explicitly specified primary script with no codepoints in the font.
  CodepointSet no_arabic = FontHelper::ToCodepointsSet(face_.get());
  no_arabic.subtract(freq_list->at("Script_arabic.riegeli"));
  hb_face_unique_ptr no_arabic_subset = Subset(face_.get(), no_arabic);
  ASSERT_TRUE(no_arabic_subset.get());

  auto config_or = AutoSegmenterConfig::GenerateConfig(
      no_arabic_subset.get(), *resolver, "Script_arabic");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(
      GetScripts(*config_or),
      UnorderedElementsAre(kLatin, kSymbols, kCyrillic, kGreek, kFallback));
  EXPECT_TRUE(GetScriptsWithInitialMergeThreshold(*config_or).empty());

  // Subset Roboto to only Cyrillic codepoints that are not in Latin, so the
  // default primary script (Script_latin) has no codepoints in the font.
  CodepointSet cyrillic_only = freq_list->at("Script_cyrillic.riegeli");
  cyrillic_only.subtract(freq_list->at("Script_latin.riegeli"));
  hb_face_unique_ptr cyrillic_subset = Subset(face_.get(), cyrillic_only);
  ASSERT_TRUE(cyrillic_subset.get());

  config_or =
      AutoSegmenterConfig::GenerateConfig(cyrillic_subset.get(), *resolver);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or), UnorderedElementsAre(kCyrillic));
  EXPECT_TRUE(GetScriptsWithInitialMergeThreshold(*config_or).empty());

  // Subset Roboto to Latin codepoints that are not covered by Language_fr.
  // Script_latin is detected, and since Language_fr has no codepoints in the
  // subset, Language_fr should not be added and Script_latin should remain.
  CodepointSet latin_without_fr = freq_list->at("Script_latin.riegeli");
  latin_without_fr.subtract(freq_list->at("Language_fr.riegeli"));
  hb_face_unique_ptr latin_no_fr_subset = Subset(face_.get(), latin_without_fr);
  ASSERT_TRUE(latin_no_fr_subset.get());

  config_or = AutoSegmenterConfig::GenerateConfig(latin_no_fr_subset.get(),
                                                  *resolver, "Language_fr");
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or), Contains(kLatin));
  EXPECT_THAT(GetScripts(*config_or), Not(Contains(kLanguageFr)));
  EXPECT_TRUE(GetScriptsWithInitialMergeThreshold(*config_or).empty());

  // Subset Roboto to a single Latin codepoint ('a'): DetectScripts requires
  // > 1 unique codepoint, but ApplyPrimaryScript still adds Script_latin
  // because its frequency data covers at least one codepoint in the font.
  hb_face_unique_ptr single_latin_subset =
      Subset(face_.get(), CodepointSet{'a'});
  ASSERT_TRUE(single_latin_subset.get());

  config_or =
      AutoSegmenterConfig::GenerateConfig(single_latin_subset.get(), *resolver);
  ASSERT_TRUE(config_or.ok()) << config_or.status();
  EXPECT_THAT(GetScripts(*config_or), UnorderedElementsAre(kLatin));
  EXPECT_THAT(GetScriptsWithInitialMergeThreshold(*config_or),
              UnorderedElementsAre("Script_latin.riegeli"));
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

TEST_F(AutoSegmenterConfigTest, MaxDepthSelection) {
  ASSERT_TRUE(cjk_face_) << "NotoSansJP-Regular.ttf not found";

  auto freq_list = BuiltInFrequenciesList(*resolver);
  ASSERT_TRUE(freq_list.ok()) << freq_list.status();

  // The full font with Script_CJK has 7 scripts + 1 uncovered + 1 feature-only
  // = 9 table keyed segments: unlimited is 2304 (> 2048), max_depth 6 reduces
  // estimated patches to 1593.
  auto config_full = AutoSegmenterConfig::GenerateConfig(
      cjk_face_.get(), *resolver, "Script_CJK");
  ASSERT_TRUE(config_full.ok()) << config_full.status();
  EXPECT_EQ(config_full->base_segmentation_plan().max_depth(), 6);

  // Collect only the codepoints covered by the 7 scripts used with Script_CJK.
  CodepointSet covered_codepoints;
  for (const auto& [ _, scripts ] :
       {kEmojiLatinAndSymbols, kGreek, kCyrillic, kUnifiedCJK, kFallback}) {
    for (const std::string& script : scripts) {
      covered_codepoints.union_set(freq_list->at(script));
    }
  }
  covered_codepoints.intersect(FontHelper::ToCodepointsSet(cjk_face_.get()));

  // Removing uncovered codepoints leaves 7 scripts + 0 uncovered + 1
  // feature-only = 8 table keyed segments: unlimited is 1024 (<= 2048), so
  // max_depth is not set.
  hb_subset_input_t* input_no_uncovered = hb_subset_input_create_or_fail();
  for (hb_codepoint_t cp : covered_codepoints) {
    hb_set_add(hb_subset_input_unicode_set(input_no_uncovered), cp);
  }
  hb_set_invert(hb_subset_input_set(input_no_uncovered,
                                    HB_SUBSET_SETS_LAYOUT_FEATURE_TAG));
  hb_face_unique_ptr subset_no_uncovered =
      make_hb_face(hb_subset_or_fail(cjk_face_.get(), input_no_uncovered));
  hb_subset_input_destroy(input_no_uncovered);

  auto config_no_uncovered = AutoSegmenterConfig::GenerateConfig(
      subset_no_uncovered.get(), *resolver, "Script_CJK");
  ASSERT_TRUE(config_no_uncovered.ok()) << config_no_uncovered.status();
  EXPECT_FALSE(config_no_uncovered->base_segmentation_plan().has_max_depth());

  // Removing non-default layout features while keeping all codepoints leaves
  // 7 scripts + 1 uncovered + 0 feature-only = 8 table keyed segments:
  // unlimited is 1024 (<= 2048), so max_depth is not set.
  hb_subset_input_t* input_no_features = hb_subset_input_create_or_fail();
  hb_set_invert(hb_subset_input_unicode_set(input_no_features));
  hb_set_clear(hb_subset_input_set(input_no_features,
                                   HB_SUBSET_SETS_LAYOUT_FEATURE_TAG));
  hb_face_unique_ptr subset_no_features =
      make_hb_face(hb_subset_or_fail(cjk_face_.get(), input_no_features));
  hb_subset_input_destroy(input_no_features);

  auto config_no_features = AutoSegmenterConfig::GenerateConfig(
      subset_no_features.get(), *resolver, "Script_CJK");
  ASSERT_TRUE(config_no_features.ok()) << config_no_features.status();
  EXPECT_FALSE(config_no_features->base_segmentation_plan().has_max_depth());


}

TEST_F(AutoSegmenterConfigTest, ConfigureMaxDepth) {
  // Verify ConfigureMaxDepth thresholds and minimum max_depth floor of 3.
  {
    SegmentationPlan plan;
    ConfigureMaxDepth(8, plan);
    EXPECT_FALSE(plan.has_max_depth());
  }
  {
    SegmentationPlan plan;
    ConfigureMaxDepth(9, plan);
    EXPECT_EQ(plan.max_depth(), 6);
  }
  {
    SegmentationPlan plan;
    ConfigureMaxDepth(10, plan);
    EXPECT_EQ(plan.max_depth(), 5);
  }
  {
    SegmentationPlan plan;
    ConfigureMaxDepth(12, plan);
    EXPECT_EQ(plan.max_depth(), 4);
  }
  {
    SegmentationPlan plan;
    ConfigureMaxDepth(20, plan);
    EXPECT_EQ(plan.max_depth(), 3);
  }
  {
    // Even when max_depth = 3 exceeds 2048 estimated patches (40 segments at
    // max_depth 3 is 2380 patches), max_depth is not reduced below 3.
    SegmentationPlan plan;
    ConfigureMaxDepth(40, plan);
    EXPECT_EQ(plan.max_depth(), 3);
  }
}

}  // namespace
}  // namespace ift::config
