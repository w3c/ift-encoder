#include "ift/config/auto_segmenter_config.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"
#include "hb.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "ift/config/load_codepoints.h"
#include "ift/config/segmenter_config.pb.h"
#include "ift/encoder/glyph_partition.h"
#include "ift/freq/unicode_frequencies.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using ift::common::CodepointSet;
using ift::common::IntSet;
using ift::common::FontHelper;
using ift::encoder::GlyphPartition;
using ift::freq::UnicodeFrequencies;

namespace ift::config {

static constexpr uint32_t kMinimumGroupSize = 4;

// clang-format off
// Quality Table:
// Quality | bigrams | simplificiation | init brotli | non init brotli | init font merge threshold | opt cut off | preprocess merging threshold
// 1       | No      | Yes             | 0           | 0               | 60%                       | 5%          | 5%
// 2       | No      | No              | 0           | 0               | 54%                       | 4%          | 4%
// 3       | Yes     | No              | 0           | 0               | 48%                       | 3%          | 3%
// 4       | Yes     | No              | 0           | 9               | 42%                       | 2%          | 2%
// 5       | Yes     | No              | 9           | 9               | 36%                       | 1%          | 1%
// 6       | Yes     | No              | 9           | 11              | 30%                       | 0.5%        | 0.5%
// 7       | Yes     | No              | 11          | 11              | 25%                       | 0.5%        | 0.05%
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
  MAX = 7,  // Alias for SEVEN
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
// - condition_analysis_mode: use DEP_GRAPH_ONLY if dependency API is available,
// otherwise CLOSURE_ONLY. DEP_GRAPH_ONLY_WITH_SIMPLIFICATION can significantly
// speed up cases with high condition complexity, at the cost of less specific
// final conditions.
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

static constexpr uint32_t DEFAULT_NETWORK_COST = 200;

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

  if (!name.empty() && std::islower(static_cast<unsigned char>(name[0]))) {
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
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

static CodepointSet NonCjkCommonCodepoints(
    const flat_hash_map<std::string, CodepointSet>& freq_list) {
  auto cjk_scripts = CjkScripts();
  flat_hash_map<hb_codepoint_t, uint32_t> unicode_counts;
  for (const auto& [file_name, script_codepoints] : freq_list) {
    if (!IsScript(file_name)) {
      continue;
    }

    if (cjk_scripts.contains(file_name)) {
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

  // This is the set of codepoints which appears in two or more non-cjk scripts
  CodepointSet non_cjk_common = NonCjkCommonCodepoints(freq_list);

  for (const auto& [file_name, script_codepoints] : freq_list) {
    if (!IsScript(file_name) && file_name != "fallback.riegeli") {
      continue;
    }
    if (file_name == "Script_CJK.riegeli@*") {
      // This is a synthetic combination of the individual CJK scripts, it's
      // only used if it's explicitly selected as the primary script.
      continue;
    }

    // For script detection look only at codepoints which are unique to this
    // script. All of CJK scripts have large overlap with each other, due
    // to this we don't exclude this CJK specific overlap from script detection.
    CodepointSet unique_codepoints = script_codepoints;
    unique_codepoints.subtract(non_cjk_common);
    unique_codepoints.intersect(unicodes);

    // TODO(garretrieger): consider using a threshold on intersection size here.
    if (unique_codepoints.size() > 1) {
      LOG(INFO) << "Script " << file_name << " is present, "
                << unique_codepoints.size() << " codepoints.";
      detected_scripts.insert(file_name);
    }
  }

  return detected_scripts;
}

// Codepoints in these categories (spaces, marks and invisible formatting
// characters) are present in many script's frequency data. Filtering
// them out from overlap detection helps avoid grouping together a large number
// of scripts unnecessarily.
static bool IgnoredForOverlapDetection(hb_codepoint_t cp) {
  switch (hb_unicode_general_category(hb_unicode_funcs_get_default(), cp)) {
    case HB_UNICODE_GENERAL_CATEGORY_CONTROL:
    case HB_UNICODE_GENERAL_CATEGORY_FORMAT:
    case HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR:
      return true;
    case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
    case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK:
      return cp >= 0x300 && cp <= 0x400;
    default:
      return false;
  }
}

// Returns the codepoints of script which are in the font and which are
// usable for overlap detection.
static CodepointSet OverlapCodepoints(
    const flat_hash_map<std::string, CodepointSet>& freq_list,
    const std::string& script, const CodepointSet& unicodes) {
  auto it = freq_list.find(script);
  if (it == freq_list.end()) {
    return CodepointSet();
  }

  CodepointSet in_script_and_font = it->second;
  in_script_and_font.intersect(unicodes);

  CodepointSet result;
  for (hb_codepoint_t cp : in_script_and_font) {
    if (!IgnoredForOverlapDetection(cp)) {
      result.insert(cp);
    }
  }

  return result;
}

// Two scripts are considered to be overlapping if the codepoints they share
// account for at least this fraction of the total probability mass of at
// least one of the two scripts.
static constexpr double kScriptOverlapThreshold = 0.10;

// Groups scripts which significantly overlap each other together. Scripts
// which don't overlap anything are placed in a group by themselves.
//
// Overlap is measured by how much of a script's probability mass is covered
// by the codepoints it shares with another script. Weighting by probability
// (instead of just counting shared codepoints) ensures that cases where
// overlapping codepoints will have a large impact on cost are processed
// together which produces better results.
//
// Grouping is important for shared codepoints: if the scripts which share
// them are placed in different merge groups then those codepoints fall back
// to heuristic merging, which produces poor results.
//
// The returned groups, and the scripts within them, are in a deterministic
// order.
static StatusOr<std::vector<std::vector<std::string>>> GroupOverlappingScripts(
    const btree_set<std::string>& detected_scripts,
    const flat_hash_map<std::string, CodepointSet>& freq_list,
    const CodepointSet& unicodes,
    const ift::common::DataFileResolver& resolver) {
  std::vector<std::string> scripts(detected_scripts.begin(),
                                   detected_scripts.end());
  std::vector<CodepointSet> codepoints;
  codepoints.reserve(scripts.size());
  for (const auto& script : scripts) {
    codepoints.push_back(OverlapCodepoints(freq_list, script, unicodes));
  }

  // Only pairs of scripts that share at least one codepoint can overlap.
  std::vector<std::pair<uint32_t, uint32_t>> candidate_pairs;
  IntSet candidate_scripts;
  for (uint32_t i = 0; i < scripts.size(); i++) {
    for (uint32_t j = i + 1; j < scripts.size(); j++) {
      if (!codepoints[i].intersects(codepoints[j])) {
        continue;
      }
      candidate_pairs.push_back(std::make_pair(i, j));
      candidate_scripts.insert(i);
      candidate_scripts.insert(j);
    }
  }

  // Frequency data is needed to weight the shared codepoints, it's expensive
  // to load so only do it for the scripts that might be grouped. Pair
  // (bigram) data isn't needed here, so use the much smaller unigram only
  // copy of the data.
  flat_hash_map<uint32_t, UnicodeFrequencies> frequencies;
  flat_hash_map<uint32_t, CodepointSet> covered_codepoints;
  flat_hash_map<uint32_t, double> total_probability;
  for (uint32_t i : candidate_scripts) {
    UnicodeFrequencies freq = TRY(
        LoadBuiltInUnigramFrequencies(scripts[i].c_str(), resolver, unicodes));

    // Note: CoveredCodepoints() recomputes the set on each call, so cache here.
    CodepointSet covered = freq.CoveredCodepoints();

    double total = 0.0;
    for (hb_codepoint_t cp : codepoints[i]) {
      if (!covered.contains(cp)) {
        continue;
      }
      total += freq.ProbabilityFor(cp);
    }

    frequencies.emplace(i, std::move(freq));
    covered_codepoints.emplace(i, std::move(covered));
    total_probability[i] = total;
  }

  GlyphPartition partition(scripts.size());
  for (const auto& [i, j] : candidate_pairs) {
    CodepointSet shared = codepoints[i];
    shared.intersect(codepoints[j]);

    double shared_probability_i = 0.0;
    double shared_probability_j = 0.0;
    for (hb_codepoint_t cp : shared) {
      if (covered_codepoints.at(i).contains(cp)) {
        shared_probability_i += frequencies.at(i).ProbabilityFor(cp);
      }
      if (covered_codepoints.at(j).contains(cp)) {
        shared_probability_j += frequencies.at(j).ProbabilityFor(cp);
      }
    }

    double fraction_i = total_probability[i] > 0.0
                            ? shared_probability_i / total_probability[i]
                            : 0.0;
    double fraction_j = total_probability[j] > 0.0
                            ? shared_probability_j / total_probability[j]
                            : 0.0;

    double fraction = std::max(fraction_i, fraction_j);
    VLOG(1) << "Overlap between " << scripts[i] << " and " << scripts[j]
            << " is " << fraction << " (" << shared.size()
            << " shared codepoints).";
    if (fraction >= kScriptOverlapThreshold) {
      LOG(INFO) << "Grouping " << scripts[i] << " and " << scripts[j]
                << " together, they share " << fraction
                << " of their probability mass.";
      TRYV(partition.Union(i, j));
    }
  }

  std::vector<std::vector<std::string>> groups;
  flat_hash_map<uint32_t, uint32_t> representative_to_group;
  for (uint32_t i = 0; i < scripts.size(); i++) {
    uint32_t representative = TRY(partition.Find(i));
    auto [it, inserted] =
        representative_to_group.insert({representative, groups.size()});
    if (inserted) {
      groups.push_back(std::vector<std::string>());
    }
    groups[it->second].push_back(scripts[i]);
  }

  return groups;
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
  // - if the primary is the unified CJK data set then all of the individual
  //   CJK scripts are replaced by it.
  detected_scripts.erase(primary_base_script);
  if (primary_script == "Script_CJK.riegeli@*") {
    for (const auto& script : CjkScripts()) {
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
    default:
      config.set_optimization_cutoff_fraction(0.005);
      break;
  }
}

static void ApplyQualityLevelTo(Quality quality, FrequencyDataConfig& config) {
  config.set_use_bigrams(quality != ONE && quality != TWO);

  if (!config.has_initial_font_merge_threshold()) {
    return;
  }

  switch (quality) {
    case ONE:
      config.set_initial_font_merge_probability_threshold(0.60);
      break;
    case TWO:
      config.set_initial_font_merge_probability_threshold(0.54);
      break;
    case THREE:
      config.set_initial_font_merge_probability_threshold(0.48);
      break;
    case FOUR:
      config.set_initial_font_merge_probability_threshold(0.42);
      break;
    case FIVE:
      config.set_initial_font_merge_probability_threshold(0.36);
      break;
    case SIX:
      config.set_initial_font_merge_probability_threshold(0.30);
      break;
    case SEVEN:
    default:
      config.set_initial_font_merge_probability_threshold(0.25);
      break;
  }
}

static void ApplyQualityLevelTo(Quality quality, MergeGroup& merge_group) {
  if (merge_group.has_cost_config()) {
    merge_group.set_preprocess_merging_group_size(kMinimumGroupSize);

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
      default:
        merge_group.set_preprocess_merging_probability_threshold(0.0005);
        break;
    }

    for (auto& freq_data :
         *merge_group.mutable_cost_config()->mutable_frequency_data()) {
      ApplyQualityLevelTo(quality, freq_data);
    }
  }
}

static void ApplyQualityLevelTo(Quality quality, SegmenterConfig& config) {
  config.set_preprocess_merging_group_size_for_ungrouped(kMinimumGroupSize * 3);

  config.set_unmapped_glyph_handling(MOVE_TO_INIT_FONT);

  if (quality == ONE) {
    config.set_condition_analysis_mode(DEP_GRAPH_ONLY_WITH_SIMPLIFICATION);
  } else {
    config.set_condition_analysis_mode(DEP_GRAPH_ONLY);
  }

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
    default:
      config.set_brotli_quality_for_initial_font_merging(11);
      break;
  }

  ApplyQualityLevelTo(quality, *config.mutable_base_heuristic_config());
  ApplyQualityLevelTo(quality, *config.mutable_ungrouped_config());
  ApplyQualityLevelTo(quality, *config.mutable_base_cost_config());

  // Based on measured network overhead cost in practice from the
  // ift demo.
  config.mutable_base_cost_config()->set_network_overhead_cost(
      DEFAULT_NETWORK_COST);
  config.mutable_base_cost_config()->set_experimental_use_patch_merges(true);

  for (auto& merge_group : *config.mutable_merge_groups()) {
    ApplyQualityLevelTo(quality, merge_group);
  }
}

static Quality AutoPickQuality(uint32_t codepoint_count) {
  // TODO(garretrieger): more sophisticated scheme for auto picking quality
  // level. roughly we want to estimate the expected cost of each quality level
  // and pick based on that. Notably probably want some measure of condition
  // complexity that feeds into the selection in addition to # of codepoints.

  // These values were picked by looking at segmenter run times by quality level
  // accross the font collection from https://github.com/google/fonts/.
  // Roughly they aim to keep the number of segmentations that take >= 10 minutes
  // for a particular count below 0.5%.
  //
  // Bin            Quality   % > 10 minutes
  // [1,      300]  7         0.3
  // (300,   1500]  6         0.3
  // (1500,  3100]  4         0.0
  // (3100, 30000]  2         0.0
  // (30000,  inf]  1        28.6
  if (codepoint_count <= 300) {
    return SEVEN;
  }
  if (codepoint_count <= 1500) {
    return SIX;
  }
  if (codepoint_count <= 3100) {
    return FOUR;
  }
  if (codepoint_count <= 30000) {
    return TWO;
  }
  return ONE;
}

// Returns the codepoints in the font which are covered by the frequency data
// of at least one of scripts.
static CodepointSet CoveredCodepoints(
    const std::vector<std::string>& scripts,
    const flat_hash_map<std::string, CodepointSet>& freq_list,
    const CodepointSet& unicodes) {
  CodepointSet result;
  for (const std::string& script : scripts) {
    auto it = freq_list.find(script);
    if (it == freq_list.end()) {
      continue;
    }
    result.union_set(it->second);
  }
  result.intersect(unicodes);
  return result;
}

// Adds a merge group to config which uses the frequency data of each of
// scripts. The script matching primary_script_file (if any) is configured to
// do initial font merging.
//
// By default the group covers all segments which the supplied frequency data
// sets have data for. If covered_codepoints is provided the group is instead
// configured to cover exactly those codepoints.
static void AddMergeGroup(const std::vector<std::string>& scripts,
                          const std::string& primary_script_file,
                          std::optional<CodepointSet> covered_codepoints,
                          SegmenterConfig& config) {
  auto* mg = config.add_merge_groups();
  auto* cost = mg->mutable_cost_config();

  std::vector<std::string> names;
  names.reserve(scripts.size());
  for (const std::string& script : scripts) {
    names.push_back(ScriptName(script));

    auto* freq_data = cost->add_frequency_data();
    freq_data->set_built_in_freq_data_name(script);
    if (script == primary_script_file) {
      freq_data->set_initial_font_merge_threshold(
          -(double)DEFAULT_NETWORK_COST * (0.8));
    }
  }

  if (covered_codepoints.has_value()) {
    // In an auto generated config there are no explicitly configured
    // segments, so the segmenter creates one segment per codepoint in the
    // font and uses the codepoint value as the segment id.
    auto* segment_ids = mg->mutable_segment_ids();
    for (hb_codepoint_t cp : *covered_codepoints) {
      segment_ids->add_values(cp);
    }
  }

  mg->set_name(absl::StrJoin(names, "+"));
}

StatusOr<SegmenterConfig> AutoSegmenterConfig::GenerateConfig(
    hb_face_t* face, const ift::common::DataFileResolver& resolver,
    std::optional<std::string> primary_script,
    std::optional<int> quality_level) {
  SegmenterConfig config;
  config.set_generate_table_keyed_segments(true);
  config.set_generate_feature_segments(true);

  auto* base_plan = config.mutable_base_segmentation_plan();
  base_plan->set_jump_ahead(2);
  base_plan->set_use_prefetch_lists(true);

  // Collect codepoints
  auto freq_list = TRY(BuiltInFrequenciesList(resolver));
  CodepointSet unicodes = FontHelper::ToCodepointsSet(face);
  uint32_t codepoint_count = unicodes.size();

  Quality quality = AutoPickQuality(codepoint_count);

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

  // Add a merge group for each group of detected scripts. Scripts which
  // overlap each other are placed into the same merge group so that their
  // shared codepoints are cost merged instead of falling back to heuristic
  // merging.
  auto script_groups = TRY(
      GroupOverlappingScripts(detected_scripts, freq_list, unicodes, resolver));
  for (const auto& script_group : script_groups) {
    if (!primary_script.has_value() || script_group.size() < 2 ||
        std::find(script_group.begin(), script_group.end(),
                  primary_script_file) == script_group.end()) {
      AddMergeGroup(script_group, primary_script_file, std::nullopt, config);
      continue;
    }

    // An explicitly specified primary script is the use case the font is
    // expected to be primarily used for, so merging should be biased towards
    // optimizing it. If it was left grouped with the other scripts it would
    // be treated as equally important as each of them, so instead give it a
    // merge group of its own.
    AddMergeGroup({primary_script_file}, primary_script_file, std::nullopt,
                  config);

    // The rest of the group still needs to be handled, but only for the
    // codepoints which the primary script's merge group doesn't already
    // cover. Those are configured explicitly so that the two groups don't
    // overlap.
    std::vector<std::string> remaining_scripts;
    for (const std::string& script : script_group) {
      if (script != primary_script_file) {
        remaining_scripts.push_back(script);
      }
    }

    CodepointSet remaining_codepoints =
        CoveredCodepoints(remaining_scripts, freq_list, unicodes);
    remaining_codepoints.subtract(
        CoveredCodepoints({primary_script_file}, freq_list, unicodes));
    if (remaining_codepoints.empty()) {
      // The primary script covers everything the rest of the group does, no
      // additional group is needed.
      continue;
    }

    AddMergeGroup(remaining_scripts, primary_script_file,
                  std::move(remaining_codepoints), config);
  }

  ApplyQualityLevelTo(quality, config);

  return config;
}

}  // namespace ift::config