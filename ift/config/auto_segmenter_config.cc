#include "ift/config/auto_segmenter_config.h"

#include <cctype>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/strip.h"
#include "hb.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/config/load_codepoints.h"
#include "ift/config/segmenter_config.pb.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using ift::common::CodepointSet;
using ift::common::FontHelper;

namespace ift::config {

static constexpr uint32_t kMinimumGroupSize = 4;

// clang-format off
// Quality Table:
// Quality | bigrams | init font merging | init brotli | non init brotli | init font merge threshold | opt cut off | preprocess merging | preprocess threshold
// 1       | No      | No                | 0           | 0               | na                        | 5%          | Yes                | 5%
// 2       | Yes     | No                | 0           | 0               | na                        | 4%          | Yes                | 4%
// 3       | Yes     | Yes               | 0           | 0               | 60%                       | 3%          | Yes                | 3%
// 4       | Yes     | Yes               | 0           | 9               | 50%                       | 2%          | Yes                | 2%
// 5       | Yes     | Yes               | 9           | 9               | 40%                       | 1%          | Yes                | 1%
// 6       | Yes     | Yes               | 9           | 11              | 30%                       | 0.5%        | Yes                | 0.5%
// 7       | Yes     | Yes               | 11          | 11              | 25%                       | 0.5%        | Yes                | 0.05%
// 8       | Yes     | Yes               | 11          | 11              | 25%                       | 0.5%        | No                 | na
// clang-format on
enum Quality {
  MIN = 1,  // Alias for ONE
  ONE = 1,
  TWO = 2,
  THREE = 3,
  FOUR = 4,
  FIVE = 5,
  SIX = 6,
  SEVEN = 7,
  EIGHT = 8,
  MAX = 8,  // Alias for EIGHT
};

// TODO(garretrieger): do something analagous to brotli quality levels
// where we define a series of levels which correspond to a set of
// values for the quality/performance tradeoff settings (including setting the
// brotli) quality level. Then we need a heuristic to pick a quality level for a
// font.
//
// If we have the ability to estimate the number of brotli ops resulting from
// a specific quality level (including a multiplier for the particular brotli
// quality) then we can select a quality level which keeps brotli ops and
// closure ops within a specific range.
//
// Then can also have a flag/input to force a specific quality level.
//
// To start, the list of parameters we can use to make quality/performance
// tradeoffs:
//
// - unmapped_glyph_handling (global)
//     Lower quality is to not find conditions (so use patch or init font), high
//     quality is to find conditions.
//
// - generate_feature_segments (global): high quality generate segment per
// feature, low quality put all optional features
//     in one segment.
//
// - brotli_quality (global)
//     Use estimated number of brotli ops (per merge group) to set this. Take
//     into account the affects of preprocess merging prior to selecting this.
//     to start use 0, 9 or 11 (avoid qualities less than 9 other than 0)
//
// - brotli_quality_for_initial_font_merging (global)
//     Use estiamted number of brotli ops for the init font processing to set
//     this (by looking at what's potentially inscope)
//
// - preprocess_merging_group_size_for_ungrouped (global)
//     Would be reasonable to always have this set to at least the minimum group
//     size.
//
// - condition_analysis_mode: always use CLOSURE_AND_DEP_GRAPH.
//
// Merge group settings:
//
// - preprocess_merging_group_size (merge group)
// - preprocess_merging_probability_threshold (merge group)
//     Set these for merge groups with very large size, using probability
//     threshold first, then group size to clamp ops to a reasonable value.
//     group size always starts at the min group size.
//
// - use_bigrams (merge group, cost)
//     Probably always want this on, use other settings instead to increase
//     performance. On very lowest quality could be disabled
//
// - optimization_cutoff_fraction (merge group, cost)
//     For now, probably ok with a global setting of somewhere around 1 to 2.5%
//     (doesn't vary).
//
// - initial_font_merge_probability_threshold (merge group, cost)
//     May be ok with a global setting, start with 50%
//
// - best_case_size_reduction_fraction (mergr group, cost)
//     Default is probably fine, but may be worth changing.
//
// - min/max patch_size (merge group, heuristic):
//     Probably fixed value for all qualities, has minimal impact on
//     performance.
//
// We may want a quality level per merge group, for the init font merge,
// and global
//
// Utilizing quality levels:
// - Have a configurable setting to the auto config call which specifies a rough
// encoding budget
//   (ie. O(1 min), O(10 min), O(1 hour)). Then try to estimate the encoding
//   time at each quality level and select the quality level which gets
//   estimated time within the budget.
// - Brotli and closure ops can both contribute significantly to overall
// segmenting times,
//   so we will need to first estimate the typical brotli and closure operation
//   time cost for the particular font (eg. run a few random closures and brotli
//   compressions)
// - Then estimate the number of ops that are needed. For closure take into
// account
//   how much savings the dep graph can provide.
// - Finall overall time can be estimated (number ops) * (op time) * (fixed
// scaling factor)
//   for both brotli and closure. Total time is the sum.

// TODO(garretrieger): to help speed up init font processing times when latin is
// primary script consider adding the latin alphabet (upper and lower) directly
// to the init font. Similar things could be done for other scripts if we can
// find data on what the "core" alphabet is.

// TODO(garretrieger): collect data on brotli compression times as a function of
// quality assuming group sizes of 4 using a CJK font

static bool IsScript(absl::string_view file_name) {
  return absl::StartsWith(file_name, "Script_");
}

static bool IsLanguage(absl::string_view file_name) {
  return absl::StartsWith(file_name, "Language_");
}

// Changes from "Script_foo.riegeli" to "Foo".
static std::string ScriptName(absl::string_view script_name) {
  if (IsScript(script_name)) {
    script_name.remove_prefix(7);
  }
  std::string name(script_name);
  size_t dot_pos = name.find('.');
  if (dot_pos != std::string::npos) {
    name = name.substr(0, dot_pos);
  }

  if (!name.empty() && std::islower(name[0])) {
    name[0] = std::toupper(name[0]);
  }
  return name;
}

static flat_hash_set<std::string> CjkScripts() {
  return {
      "Script_CJK.riegeli@*",
      "Script_japanese.riegeli@*",
      "Script_korean.riegeli@*",
      "Script_chinese-simplified.riegeli@*",
      "Script_chinese-traditional.riegeli@*",
  };
}

static CodepointSet CommonCodepoints(
    const flat_hash_map<std::string, CodepointSet>& freq_list, bool cjk_only) {
  auto cjk_scripts = CjkScripts();
  flat_hash_map<hb_codepoint_t, uint32_t> unicode_counts;
  for (const auto& [file_name, script_codepoints] : freq_list) {
    if (!IsScript(file_name)) {
      continue;
    }

    if (file_name == "Script_CJK.riegeli@*") {
      // this is a combination of CJK so ignore for the purposes of common
      // codepoints.
      continue;
    }

    bool is_cjk = cjk_scripts.contains(file_name);
    if (cjk_only && !is_cjk) {
      continue;
    }

    for (hb_codepoint_t u : script_codepoints) {
      unicode_counts[u]++;
    }
  }

  CodepointSet common_codepoints;
  for (const auto& [u, count] : unicode_counts) {
    if (count > 1) {
      common_codepoints.insert(u);
    }
  }

  return common_codepoints;
}

static btree_set<std::string> DetectScripts(
    const flat_hash_map<std::string, CodepointSet>& freq_list,
    const CodepointSet& unicodes) {
  btree_set<std::string> detected_scripts;
  flat_hash_set<std::string> detected_cjk_scripts;

  CodepointSet common = CommonCodepoints(freq_list, false);
  auto cjk_scripts = CjkScripts();

  for (const auto& [file_name, script_codepoints] : freq_list) {
    if (!IsScript(file_name) && file_name != "fallback.riegeli") {
      continue;
    }
    if (file_name == "Script_CJK.riegeli@*") {
      // special cased later.
      continue;
    }

    // To avoid false positives on fonts with common ASCII/punctuation,
    // only consider codepoints outside the basic Latin range for detection.
    CodepointSet unique_codepoints = script_codepoints;
    unique_codepoints.subtract(common);

    CodepointSet intersection = unique_codepoints;
    intersection.intersect(unicodes);

    // TODO(garretrieger): consider using a threshold on intersection size here.
    if (intersection.size() > 1) {
      LOG(INFO) << "Script " << file_name << " is present, "
                << intersection.size() << " codepoints.";
      detected_scripts.insert(file_name);
      if (cjk_scripts.contains(file_name)) {
        detected_cjk_scripts.insert(file_name);
      }
    }
  }

  // Since the language specific CJK scripts all overlap if we have detected
  // more than one, or the only codepoints present are common to all cjk scripts
  // then replace the language specific scripts with the unified CJK script.
  CodepointSet only_cjk_common = CommonCodepoints(freq_list, true);
  only_cjk_common.subtract(common);
  if (detected_cjk_scripts.size() > 1 ||
      (detected_cjk_scripts.empty() && only_cjk_common.intersects(unicodes))) {
    // upgrade from individual CJK scripts to the unified one.
    for (const auto& script : detected_cjk_scripts) {
      detected_scripts.erase(script);
    }

    LOG(INFO) << "Script_CJK.riegeli@* added to detected list.";
    detected_scripts.insert("Script_CJK.riegeli@*");
  }

  return detected_scripts;
}

static StatusOr<std::string> FindFileName(
    absl::string_view base_name,
    const flat_hash_map<std::string, CodepointSet>& built_in_freqs) {
  for (const auto& [file_name, _] : built_in_freqs) {
    if (absl::StartsWith(file_name, base_name) &&
        (file_name.size() == base_name.size() ||
         file_name[base_name.size()] == '.')) {
      return file_name;
    }
  }
  return absl::NotFoundError(
      absl::StrCat("Freq file for ", base_name, " was not found."));
}

StatusOr<std::string> AutoSegmenterConfig::GetBaseScriptForLanguage(
    absl::string_view language) {
  if (absl::EndsWith(language, ".riegeli")) {
    language = absl::StripSuffix(language, ".riegeli");
  }
  if (absl::EndsWith(language, ".riegeli@*")) {
    language = absl::StripSuffix(language, ".riegeli@*");
  }

  static const auto* lang_to_script =
      new flat_hash_map<std::string, std::string>{
          {"Language_af", "Script_latin"},
          {"Language_ak", "Script_latin"},
          {"Language_am", "Script_ethiopic"},
          {"Language_ar", "Script_arabic"},
          {"Language_ar-Latn", "Script_latin"},
          {"Language_as", "Script_bengali"},
          {"Language_ay", "Script_latin"},
          {"Language_az", "Script_latin"},
          {"Language_be", "Script_cyrillic"},
          {"Language_bg", "Script_cyrillic"},
          {"Language_bg-Latn", "Script_latin"},
          {"Language_bho", "Script_devanagari"},
          {"Language_bm", "Script_latin"},
          {"Language_bn", "Script_bengali"},
          {"Language_bn-Latn", "Script_latin"},
          {"Language_bs", "Script_latin"},
          {"Language_ca", "Script_latin"},
          {"Language_ceb", "Script_latin"},
          {"Language_ckb", "Script_arabic"},
          {"Language_co", "Script_latin"},
          {"Language_cs", "Script_latin"},
          {"Language_cy", "Script_latin"},
          {"Language_da", "Script_latin"},
          {"Language_de", "Script_latin"},
          {"Language_doi", "Script_devanagari"},
          {"Language_dv", "Script_thaana"},
          {"Language_ee", "Script_latin"},
          {"Language_el", "Script_greek"},
          {"Language_el-Latn", "Script_latin"},
          {"Language_en", "Script_latin"},
          {"Language_en-Cyrl", "Script_cyrillic"},
          {"Language_eo", "Script_latin"},
          {"Language_es", "Script_latin"},
          {"Language_et", "Script_latin"},
          {"Language_eu", "Script_latin"},
          {"Language_fa", "Script_arabic"},
          {"Language_ff", "Script_latin"},
          {"Language_fi", "Script_latin"},
          {"Language_fil", "Script_latin"},
          {"Language_fr", "Script_latin"},
          {"Language_fy", "Script_latin"},
          {"Language_ga", "Script_latin"},
          {"Language_gd", "Script_latin"},
          {"Language_gl", "Script_latin"},
          {"Language_gn", "Script_latin"},
          {"Language_gu", "Script_gujarati"},
          {"Language_gu-Latn", "Script_latin"},
          {"Language_ha", "Script_latin"},
          {"Language_haw", "Script_latin"},
          {"Language_hi", "Script_devanagari"},
          {"Language_hi-Latn", "Script_latin"},
          {"Language_hmn", "Script_latin"},
          {"Language_hr", "Script_latin"},
          {"Language_ht", "Script_latin"},
          {"Language_hu", "Script_latin"},
          {"Language_hy", "Script_armenian"},
          {"Language_id", "Script_latin"},
          {"Language_ig", "Script_latin"},
          {"Language_ilo", "Script_latin"},
          {"Language_is", "Script_latin"},
          {"Language_it", "Script_latin"},
          {"Language_iw", "Script_hebrew"},
          {"Language_ja", "Script_japanese"},
          {"Language_ja-Latn", "Script_latin"},
          {"Language_jv", "Script_latin"},
          {"Language_ka", "Script_georgian"},
          {"Language_kk", "Script_cyrillic"},
          {"Language_kl", "Script_latin"},
          {"Language_km", "Script_khmer"},
          {"Language_kn", "Script_kannada"},
          {"Language_kn-Latn", "Script_latin"},
          {"Language_ko", "Script_korean"},
          {"Language_kok", "Script_devanagari"},
          {"Language_kri", "Script_latin"},
          {"Language_ku", "Script_latin"},
          {"Language_ky", "Script_cyrillic"},
          {"Language_la", "Script_latin"},
          {"Language_lb", "Script_latin"},
          {"Language_lg", "Script_latin"},
          {"Language_ln", "Script_latin"},
          {"Language_lo", "Script_lao"},
          {"Language_lt", "Script_latin"},
          {"Language_lus", "Script_latin"},
          {"Language_lv", "Script_latin"},
          {"Language_mai", "Script_devanagari"},
          {"Language_mg", "Script_latin"},
          {"Language_mi", "Script_latin"},
          {"Language_mk", "Script_cyrillic"},
          {"Language_ml", "Script_malayalam"},
          {"Language_ml-Latn", "Script_latin"},
          {"Language_mn", "Script_cyrillic"},
          {"Language_mni-Mtei", "Script_meetei-mayek"},
          {"Language_mr", "Script_devanagari"},
          {"Language_mr-Latn", "Script_latin"},
          {"Language_ms", "Script_latin"},
          {"Language_mt", "Script_latin"},
          {"Language_my", "Script_myanmar"},
          {"Language_ne", "Script_devanagari"},
          {"Language_nl", "Script_latin"},
          {"Language_no", "Script_latin"},
          {"Language_nso", "Script_latin"},
          {"Language_ny", "Script_latin"},
          {"Language_om", "Script_latin"},
          {"Language_or", "Script_oriya"},
          {"Language_pa", "Script_gurmukhi"},
          {"Language_pl", "Script_latin"},
          {"Language_ps", "Script_arabic"},
          {"Language_pt", "Script_latin"},
          {"Language_qu", "Script_latin"},
          {"Language_ro", "Script_latin"},
          {"Language_ru", "Script_cyrillic"},
          {"Language_ru-Latn", "Script_latin"},
          {"Language_rw", "Script_latin"},
          {"Language_sa", "Script_devanagari"},
          {"Language_sd", "Script_arabic"},
          {"Language_si", "Script_sinhala"},
          {"Language_sk", "Script_latin"},
          {"Language_sl", "Script_latin"},
          {"Language_sm", "Script_latin"},
          {"Language_sn", "Script_latin"},
          {"Language_so", "Script_latin"},
          {"Language_sq", "Script_latin"},
          {"Language_sr", "Script_cyrillic"},
          {"Language_st", "Script_latin"},
          {"Language_su", "Script_latin"},
          {"Language_sv", "Script_latin"},
          {"Language_sw", "Script_latin"},
          {"Language_ta", "Script_tamil"},
          {"Language_ta-Latn", "Script_latin"},
          {"Language_te", "Script_telugu"},
          {"Language_te-Latn", "Script_latin"},
          {"Language_tg", "Script_cyrillic"},
          {"Language_th", "Script_thai"},
          {"Language_ti", "Script_ethiopic"},
          {"Language_tk", "Script_latin"},
          {"Language_tr", "Script_latin"},
          {"Language_ts", "Script_latin"},
          {"Language_tt", "Script_cyrillic"},
          {"Language_ug", "Script_arabic"},
          {"Language_uk", "Script_cyrillic"},
          {"Language_ur", "Script_arabic"},
          {"Language_uz", "Script_latin"},
          {"Language_vi", "Script_latin"},
          {"Language_xh", "Script_latin"},
          {"Language_yi", "Script_hebrew"},
          {"Language_yo", "Script_latin"},
          {"Language_zh-Hani", "Script_chinese-simplified"},
          {"Language_zh-Hans", "Script_chinese-simplified"},
          {"Language_zh-Hant", "Script_chinese-traditional"},
          {"Language_zh-Latn", "Script_latin"},
          {"Language_zu", "Script_latin"},
      };
  auto it = lang_to_script->find(std::string(language));
  if (it != lang_to_script->end()) {
    return it->second;
  }
  return absl::NotFoundError(
      absl::StrCat("Unable to find base script for ", language));
}

static Status ApplyPrimaryScript(
    const flat_hash_map<std::string, CodepointSet>& freq_list,
    std::string primary_script, btree_set<std::string>& detected_scripts) {
  std::string primary_base_script = "";
  if (IsLanguage(primary_script)) {
    primary_base_script = TRY(FindFileName(
        TRY(AutoSegmenterConfig::GetBaseScriptForLanguage(primary_script)),
        freq_list));
  } else if (IsScript(primary_script)) {
    primary_base_script = TRY(FindFileName(primary_script, freq_list));
  } else {
    return absl::InternalError(
        absl::StrCat("Unknown freq file type: ", primary_script));
  }

  primary_script = TRY(FindFileName(primary_script, freq_list));
  LOG(INFO) << "Primary script/language: " << primary_script;
  LOG(INFO) << "Primary base script is " << primary_base_script;

  // Primary script behaviour:
  // - base script if present is replaced by primary script.
  // - if base script is CJK, then all CJK's are replaced by primary script
  detected_scripts.erase(primary_base_script);
  auto cjk_scripts = CjkScripts();
  if (cjk_scripts.contains(primary_base_script)) {
    for (const auto& script : cjk_scripts) {
      detected_scripts.erase(script);
    }
  }

  detected_scripts.insert(primary_script);

  return absl::OkStatus();
}

static void ApplyQualityLevelTo(Quality quality,
                                HeuristicConfiguration& config) {
  config.set_min_patch_size(2500);
}

static void ApplyQualityLevelTo(Quality quality, CostConfiguration& config) {
  config.set_min_group_size(kMinimumGroupSize);

  if (quality == ONE) {
    config.set_use_bigrams(false);
  } else {
    config.set_use_bigrams(true);
  }

  switch (quality) {
    case ONE:
      config.set_optimization_cutoff_fraction(0.05);
      break;
    case TWO:
      config.set_optimization_cutoff_fraction(0.04);
      break;
    case THREE:
      config.set_optimization_cutoff_fraction(0.03);
      break;
    case FOUR:
      config.set_optimization_cutoff_fraction(0.02);
      break;
    case FIVE:
      config.set_optimization_cutoff_fraction(0.01);
      break;
    case SIX:
    case SEVEN:
    case EIGHT:
    default:
      config.set_optimization_cutoff_fraction(0.005);
      break;
  }
}

static void ApplyQualityLevelTo(Quality quality, MergeGroup& merge_group) {
  if (merge_group.has_cost_config()) {
    if (quality == ONE || quality == TWO) {
      merge_group.mutable_cost_config()->clear_initial_font_merge_threshold();
    }

    if (quality >= ONE && quality <= SEVEN) {
      merge_group.set_preprocess_merging_group_size(kMinimumGroupSize);
    } else {
      merge_group.set_preprocess_merging_group_size(1);
    }

    switch (quality) {
      case ONE:
        merge_group.set_preprocess_merging_probability_threshold(0.05);
        break;
      case TWO:
        merge_group.set_preprocess_merging_probability_threshold(0.04);
        break;
      case THREE:
        merge_group.set_preprocess_merging_probability_threshold(0.03);
        break;
      case FOUR:
        merge_group.set_preprocess_merging_probability_threshold(0.02);
        break;
      case FIVE:
        merge_group.set_preprocess_merging_probability_threshold(0.01);
        break;
      case SIX:
        merge_group.set_preprocess_merging_probability_threshold(0.005);
        break;
      case SEVEN:
        merge_group.set_preprocess_merging_probability_threshold(0.0005);
        break;
      case EIGHT:
      default:
        merge_group.clear_preprocess_merging_probability_threshold();
        break;
    }

    if (merge_group.mutable_cost_config()->has_initial_font_merge_threshold()) {
      switch (quality) {
        case THREE:
          merge_group.mutable_cost_config()
              ->set_initial_font_merge_probability_threshold(0.60);
          break;
        case FOUR:
          merge_group.mutable_cost_config()
              ->set_initial_font_merge_probability_threshold(0.50);
          break;
        case FIVE:
          merge_group.mutable_cost_config()
              ->set_initial_font_merge_probability_threshold(0.40);
          break;
        case SIX:
          merge_group.mutable_cost_config()
              ->set_initial_font_merge_probability_threshold(0.30);
          break;
        case SEVEN:
        case EIGHT:
        default:
          merge_group.mutable_cost_config()
              ->set_initial_font_merge_probability_threshold(0.25);
          break;
      }
    }
  }
}

static void ApplyQualityLevelTo(Quality quality, SegmenterConfig& config) {
  config.set_preprocess_merging_group_size_for_ungrouped(kMinimumGroupSize*3);

  config.set_unmapped_glyph_handling(MOVE_TO_INIT_FONT);

  switch (quality) {
    case ONE:
    case TWO:
    case THREE:
      config.set_brotli_quality(0);
      break;
    case FOUR:
    case FIVE:
      config.set_brotli_quality(9);
      break;
    case SIX:
    case SEVEN:
    case EIGHT:
    default:
      config.set_brotli_quality(11);
      break;
  }

  switch (quality) {
    case ONE:
    case TWO:
    case THREE:
    case FOUR:
      config.set_brotli_quality_for_initial_font_merging(0);
      break;
    case FIVE:
    case SIX:
      config.set_brotli_quality_for_initial_font_merging(9);
      break;
    case SEVEN:
    case EIGHT:
    default:
      config.set_brotli_quality_for_initial_font_merging(11);
      break;
  }

  ApplyQualityLevelTo(quality, *config.mutable_base_heuristic_config());
  ApplyQualityLevelTo(quality, *config.mutable_ungrouped_config());
  ApplyQualityLevelTo(quality, *config.mutable_base_cost_config());

  for (auto& merge_group : *config.mutable_merge_groups()) {
    ApplyQualityLevelTo(quality, merge_group);
  }
}

StatusOr<SegmenterConfig> AutoSegmenterConfig::GenerateConfig(
    hb_face_t* face, std::optional<std::string> primary_script,
    std::optional<int> quality_level) {
  SegmenterConfig config;
  config.set_generate_table_keyed_segments(true);
  config.set_generate_feature_segments(true);
  config.set_condition_analysis_mode(DEP_GRAPH_ONLY);

  auto* base_plan = config.mutable_base_segmentation_plan();
  base_plan->set_jump_ahead(2);
  base_plan->set_use_prefetch_lists(true);

  // Collect codepoints
  auto freq_list = TRY(BuiltInFrequenciesList());
  CodepointSet unicodes = FontHelper::ToCodepointsSet(face);
  uint32_t cp_count = unicodes.size();

  // TODO(garretrieger): more sophisticated scheme for auto picking quality
  // level. roughly we want to estimate the expected cost of each quality level
  // and pick based on that.
  Quality quality = THREE;
  if (cp_count <= 1000) {
    quality = MAX;
  } else if (cp_count <= 3000) {
    quality = SIX;
  }

  if (quality_level.has_value() && quality_level.value() >= MIN &&
      quality_level.value() <= MAX) {
    quality = static_cast<Quality>(quality_level.value());
    VLOG(0) << "Using specified quality level for segmenting: " << quality;
  } else {
    VLOG(0) << "Quality level unspecified, auto picked: " << quality;
  }

  // Detect scripts by intersection with frequency data
  btree_set<std::string> detected_scripts = DetectScripts(freq_list, unicodes);

  // Quality tradeoffs based on codepoint count
  // TODO(garretrieger): alternate approach - estimate the number of brotli ops
  // (including accounting for pairs only within merge groups), and then select
  // the cutoffs and premerging to keep the number of brotli ops within a
  // specific range.

  TRYV(ApplyPrimaryScript(freq_list, primary_script.value_or("Script_latin"),
                          detected_scripts));
  std::string primary_script_file =
      TRY(FindFileName(primary_script.value_or("Script_latin"), freq_list));

  // Add merge groups for other detected scripts
  for (const std::string& script : detected_scripts) {
    auto* mg = config.add_merge_groups();
    mg->set_name(ScriptName(script));
    auto* cost = mg->mutable_cost_config();

    cost->set_built_in_freq_data_name(script);
    if (script == primary_script_file) {
      cost->set_initial_font_merge_threshold(-60);
    }
  }

  ApplyQualityLevelTo(quality, config);

  return config;
}

}  // namespace ift::config