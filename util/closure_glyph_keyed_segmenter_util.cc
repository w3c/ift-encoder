#include <google/protobuf/text_format.h>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/int_set.h"
#include "common/try.h"
#include "hb.h"
#include "ift/encoder/activation_condition.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/compiler.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/merge_strategy.h"
#include "ift/encoder/subset_definition.h"
#include "ift/freq/bigram_probability_calculator.h"
#include "ift/freq/unicode_frequencies.h"
#include "ift/proto/patch_encoding.h"
#include "ift/proto/patch_map.h"
#include "ift/url_template.h"
#include "util/load_codepoints.h"
#include "util/segmentation_plan.pb.h"

/*
 * Given a code point based segmentation creates an appropriate glyph based
 * segmentation and associated activation conditions that maintain the "closure
 * requirement".
 */

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Name of the font to convert to IFT.");

ABSL_FLAG(bool, output_segmentation_plan, false,
          "If set a segmentation plan representing the determined segmentation "
          "will be output to stdout.");

ABSL_FLAG(bool, include_initial_codepoints_in_config, true,
          "If set the generated encoder config will include the initial "
          "codepoint set.");

ABSL_FLAG(bool, output_segmentation_analysis, true,
          "If set an analysis of the segmentation will be output to stderr.");

ABSL_FLAG(std::string, initial_codepoints_file, "",
          "Path to a file which defines the desired set of codepoints in the "
          "initial font.");

ABSL_FLAG(
    std::string, codepoints_file, "",
    "Path to a file which defines the desired codepoint based segmentation.");

ABSL_FLAG(std::string, frequency_data_file, "",
          "Path to a file which contains codepoint frequency data in Riegeli "
          "format.");

ABSL_FLAG(uint32_t, number_of_segments, 2,
          "Number of segments to split the input codepoints into.");

ABSL_FLAG(uint32_t, min_patch_size_bytes, 0,
          "The segmenter will try to increase patch sizes to at least this "
          "amount via merging if needed.");

ABSL_FLAG(uint32_t, max_patch_size_bytes, UINT32_MAX,
          "The segmenter will avoid merges which result in patches larger than "
          "this amount.");

ABSL_FLAG(double, optimization_cutoff_fraction, 0.001,
          "Stop optimizing segments with total cost below this fraction of "
          "the total cost. Used to speedup processing time by skipping "
          "optimization of segments that have very little contribution to "
          "the total segmentation cost.");

enum MergingStrategy {
  HEURISTIC,
  COST,
  COST_BIGRAM,
};

std::string AbslUnparseFlag(MergingStrategy severity) {
  switch (severity) {
    case HEURISTIC:
      return "heuristic";
    case COST:
      return "cost";
    case COST_BIGRAM:
      return "cost_bigram";
  }
  return "";  // Should be unreachable
}

bool AbslParseFlag(absl::string_view text, MergingStrategy* dst,
                   std::string* error) {
  if (text == "heuristic") {
    *dst = HEURISTIC;
    return true;
  }
  if (text == "cost") {
    *dst = COST;
    return true;
  }
  if (text == "cost_bigram") {
    *dst = COST_BIGRAM;
    return true;
  }

  *error = "Value must be one of: heuristic, cost, cost_bigram.";
  return false;
}

ABSL_FLAG(
    MergingStrategy, merging_strategy, HEURISTIC,
    "Sets the strategy that is used to decide how to merge patches together. "
    "There are three available strategies:\n"
    " - heuristic (default): this strategy uses a simple heuristic that aims "
    "to keep patch sizes within a supplied min and max. This is the default "
    "and fastest strategy. Incorporates the ordering of input "
    "codepoints, but does not use frequency data.\n"
    " - cost: uses a probability based cost function and selects merges "
    "that reduce overal cost. Utilizes individual codepoint frequency data."
    "Does not enforce patch size min and max. Slower than the "
    "HEURISTIC approach.\n"
    " - cost_bigram: a refinement of the COST approach that uses a "
    "more accurate cost function based on codepoint bigram probabilities. "
    "This incorporates information an codepoint co-occurence. However, "
    "the approach is more computationally costly than the COST strategy.");

ABSL_FLAG(uint32_t, network_overhead_cost, 75,
          "When picking merges via the cost method this is the cost in bytes "
          "for each network request.");

// TODO(garretrieger): add additional setting for cost base merging that
// configures a minimum grouping size (in terms of number of codepoints).

ABSL_FLAG(std::vector<std::string>, optional_feature_tags, {},
          "A list of feature tags which can be optionally added to the font "
          "via patch.");

using absl::btree_map;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::CodepointSet;
using common::FontData;
using common::FontHelper;
using common::GlyphSet;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::IntSet;
using common::make_hb_blob;
using google::protobuf::TextFormat;
using ift::URLTemplate;
using ift::encoder::ActivationCondition;
using ift::encoder::ClosureGlyphSegmenter;
using ift::encoder::Compiler;
using ift::encoder::GlyphSegmentation;
using ift::encoder::MergeStrategy;
using ift::encoder::Segment;
using ift::encoder::SubsetDefinition;
using ift::freq::BigramProbabilityCalculator;
using ift::freq::UnicodeFrequencies;
using ift::proto::PatchEncoding;
using ift::proto::PatchMap;
using util::CodepointAndFrequency;

static bool FrequenciesAreRequired() {
  MergingStrategy strategy = absl::GetFlag(FLAGS_merging_strategy);
  return (strategy == COST) || (strategy == COST_BIGRAM);
}

StatusOr<std::vector<CodepointAndFrequency>> TargetCodepoints(
    hb_face_t* font, const std::string& codepoints_file,
    const IntSet& init_codepoints) {
  IntSet font_unicodes = FontHelper::ToCodepointsSet(font);

  std::vector<CodepointAndFrequency> codepoints_filtered;
  if (!codepoints_file.empty()) {
    auto codepoints = TRY(util::LoadCodepointsOrdered(codepoints_file.c_str()));
    if (FrequenciesAreRequired()) {
      // When frequencies are used we want codepoints sorted by freq.
      std::sort(codepoints.begin(), codepoints.end());
    }
    for (const auto& cp : codepoints) {
      if (FrequenciesAreRequired() && !cp.frequency.has_value()) {
        return absl::InvalidArgumentError(
            "When using cost based merging codepoint frequency data must be "
            "supplied. Missing for " +
            std::to_string(cp.codepoint));
      }

      if (font_unicodes.contains(cp.codepoint) &&
          !init_codepoints.contains(cp.codepoint)) {
        codepoints_filtered.push_back(cp);
      }
    }
  } else {
    // No codepoints file, just use the full set of codepoints supported by the
    // font.
    for (uint32_t cp : font_unicodes) {
      if (!init_codepoints.contains(cp)) {
        codepoints_filtered.push_back(CodepointAndFrequency{cp, std::nullopt});
      }
    }
  }
  return codepoints_filtered;
}

StatusOr<hb_face_unique_ptr> LoadFont(const char* filename) {
  return TRY(util::LoadFile(filename)).face();
}

constexpr uint32_t NETWORK_REQUEST_BYTE_OVERHEAD = 75;

StatusOr<int> EncodingSize(const GlyphSegmentation* segmentation,
                           const Compiler::Encoding& encoding) {
  // There are three parts to the cost of a segmentation:
  // - Size of the glyph keyed mapping table.
  // - Total size of all glyph keyed patches
  // - Network overhead (fixed cost per patch).
  auto init_font = encoding.init_font.face();

  btree_map<std::string, uint32_t> url_to_size;
  uint32_t total_size = 0;
  uint32_t base_size = 0;
  uint32_t conditional_size = 0;
  uint32_t fallback_size = 0;
  for (const auto& [url, data] : encoding.patches) {
    if (url.substr(url.size() - 2) == "gk") {
      total_size += data.size() + NETWORK_REQUEST_BYTE_OVERHEAD;
      url_to_size[url] = data.size();
    }
  }

  if (segmentation != nullptr) {
    btree_map<ift::encoder::patch_id_t, std::pair<std::string, int>>
        patch_id_to_url;
    for (const auto& condition : segmentation->Conditions()) {
      std::string url = *URLTemplate::PatchToUrl(
          std::vector<uint8_t>{2, '1', '_', 128, 7, '.', 'i', 'f', 't', '_',
                               'g', 'k'},
          condition.activated());

      int type =
          condition.IsExclusive() ? 0 : (!condition.IsFallback() ? 1 : 2);
      patch_id_to_url[condition.activated()] = std::pair(url, type);
    }

    for (const auto& [id, pair] : patch_id_to_url) {
      const std::string& url = pair.first;
      int type = pair.second;
      auto url_size = url_to_size.find(url);
      if (url_size == url_to_size.end()) {
        return absl::InternalError("URL is missing: " + url);
      }

      const char* id_postfix = (type == 0) ? "*" : ((type == 1) ? "" : "f");
      if (type == 0) {
        base_size += url_size->second;
      }
      if (type == 1) {
        conditional_size += url_size->second;
      }
      if (type == 2) {
        fallback_size += url_size->second;
      }

      fprintf(stderr, "  patch %s (p%u%s) adds %u bytes, %u bytes overhead\n",
              url.c_str(), id, id_postfix, url_size->second,
              NETWORK_REQUEST_BYTE_OVERHEAD);
    }
  } else {
    for (const auto& [url, size] : url_to_size) {
      fprintf(stderr, "  patch %s adds %u bytes, %u bytes overhead\n",
              url.c_str(), size, NETWORK_REQUEST_BYTE_OVERHEAD);
    }
  }

  auto iftx =
      FontHelper::TableData(init_font.get(), HB_TAG('I', 'F', 'T', 'X'));
  total_size += iftx.size();
  fprintf(stderr, "  mapping table: %u bytes\n", iftx.size());

  if (segmentation != nullptr) {
    double base_percent = ((double)base_size / (double)total_size) * 100.0;
    double conditional_percent =
        ((double)conditional_size / (double)total_size) * 100.0;
    double fallback_percent =
        ((double)fallback_size / (double)total_size) * 100.0;
    fprintf(stderr, "  base patches total size:        %u bytes (%f%%)\n",
            base_size, base_percent);
    fprintf(stderr, "  conditional patches total size: %u bytes (%f%%)\n",
            conditional_size, conditional_percent);
    fprintf(stderr, "  fallback patch total size:      %u bytes (%f%%)\n",
            fallback_size, fallback_percent);
  }

  return total_size;
}

// The "ideal" segmentation is one where if we could ignore the glyph closure
// requirement then the glyphs could be evenly distributed between the desired
// number of input segments. This should minimize overhead.
StatusOr<int> IdealSegmentationSize(hb_face_t* font,
                                    const GlyphSegmentation& segmentation,
                                    uint32_t number_input_segments) {
  fprintf(stderr, "IdealSegmentationSize():\n");
  IntSet glyphs;
  for (const auto& [id, glyph_set] : segmentation.GidSegments()) {
    glyphs.union_set(glyph_set);
  }

  uint32_t glyphs_per_patch = glyphs.size() / number_input_segments;
  uint32_t remainder_glyphs = glyphs.size() % number_input_segments;

  Compiler compiler;
  compiler.SetFace(font);

  IntSet all_unicodes;

  TRYV(compiler.SetInitSubset(IntSet{}));

  auto glyphs_it = glyphs.begin();
  for (uint32_t i = 0; i < number_input_segments; i++) {
    auto begin = glyphs_it;
    glyphs_it = std::next(glyphs_it, glyphs_per_patch);
    if (remainder_glyphs > 0) {
      glyphs_it++;
      remainder_glyphs--;
    }

    GlyphSet gids;
    gids.insert(begin, glyphs_it);
    auto unicodes = FontHelper::GidsToUnicodes(font, gids);

    TRYV(compiler.AddGlyphDataPatch(i, gids));
    all_unicodes.insert(unicodes.begin(), unicodes.end());

    TRYV(compiler.AddGlyphDataPatchCondition(
        PatchMap::Entry(unicodes, i, PatchEncoding::GLYPH_KEYED)));
  }

  compiler.AddNonGlyphDataSegment(all_unicodes);

  auto encoding = TRY(compiler.Compile());
  return EncodingSize(nullptr, encoding);
}

uint32_t NumExclusivePatches(const GlyphSegmentation& segmentation) {
  uint32_t count = 0;
  for (const auto& condition : segmentation.Conditions()) {
    if (condition.IsExclusive()) {
      count++;
    }
  }
  return count;
}

StatusOr<int> SegmentationSize(hb_face_t* font,
                               const GlyphSegmentation& segmentation) {
  fprintf(stderr, "SegmentationSize():\n");
  Compiler compiler;
  compiler.SetFace(font);

  IntSet all_segments;

  TRYV(compiler.SetInitSubset(IntSet{}));

  for (const auto& [id, glyph_set] : segmentation.GidSegments()) {
    IntSet s;
    s.insert(glyph_set.begin(), glyph_set.end());
    TRYV(compiler.AddGlyphDataPatch(id, s));
    all_segments.insert(id);
  }

  SubsetDefinition all;
  for (const auto& s : segmentation.Segments()) {
    all.Union(s);
  }
  compiler.AddNonGlyphDataSegment(all);

  std::vector<ActivationCondition> conditions;
  for (const auto& c : segmentation.Conditions()) {
    conditions.push_back(c);
  }

  flat_hash_map<uint32_t, SubsetDefinition> segments;
  uint32_t i = 0;
  for (const auto& s : segmentation.Segments()) {
    segments[i++] = s;
  }

  auto entries = TRY(ActivationCondition::ActivationConditionsToPatchMapEntries(
      conditions, segments));
  for (const auto& e : entries) {
    TRYV(compiler.AddGlyphDataPatchCondition(e));
  }

  auto encoding = TRY(compiler.Compile());

  return EncodingSize(&segmentation, encoding);
}

StatusOr<std::vector<SubsetDefinition>> GroupCodepoints(
    std::vector<CodepointAndFrequency> codepoints,
    uint32_t number_of_segments) {
  uint32_t per_group = codepoints.size() / number_of_segments;
  uint32_t remainder = codepoints.size() % number_of_segments;

  if (per_group > 1 && FrequenciesAreRequired()) {
    return absl::InvalidArgumentError(
        "When using codepoint frequencies there must only be one codepoint per "
        "segment.");
  }

  std::vector<SubsetDefinition> out;
  auto end = codepoints.begin();
  for (uint32_t i = 0; i < number_of_segments && end != codepoints.end(); i++) {
    auto start = end;
    end = std::next(end, per_group);
    if (remainder > 0) {
      end++;
      remainder--;
    }

    SubsetDefinition def;
    for (; start != end; start++) {
      def.codepoints.insert(start->codepoint);
    }

    if (!def.Empty()) {
      out.push_back(def);
    }
  }

  return out;
}

UnicodeFrequencies ToFrequencies(
    const std::vector<CodepointAndFrequency>& cps) {
  UnicodeFrequencies frequencies;
  for (const auto& cp : cps) {
    if (cp.frequency.has_value()) {
      frequencies.Add(cp.codepoint, cp.codepoint, *cp.frequency);
    }
  }
  return frequencies;
}

// Loads unicode frequency data from either a dedicated frequency data file or
// from the codepoint and frequency entries if no data file is given.
StatusOr<UnicodeFrequencies> GetFrequencyData(
    const std::string& frequency_data_file,
    const std::vector<CodepointAndFrequency>& cps) {
  if (frequency_data_file.empty()) {
    return ToFrequencies(cps);
  }

  return util::LoadFrequenciesFromRiegeli(frequency_data_file.c_str());
}

// Analysis of segmentation that does not utilize codepoint frequencies.
static int NonFrequencyAnalysis(hb_face_t* font,
                                const GlyphSegmentation& segmentation) {
  auto cost = SegmentationSize(font, segmentation);
  if (!cost.ok()) {
    std::cerr << "Failed to compute segmentation cost: " << cost.status()
              << std::endl;
    return -1;
  }
  auto ideal_cost = IdealSegmentationSize(font, segmentation,
                                          NumExclusivePatches(segmentation));
  if (!ideal_cost.ok()) {
    std::cerr << "Failed to compute ideal segmentation cost: " << cost.status()
              << std::endl;
    return -1;
  }

  std::cerr << std::endl;
  std::cerr << "glyphs_in_fallback = " << segmentation.UnmappedGlyphs().size()
            << std::endl;
  std::cerr << "ideal_cost_bytes = " << *ideal_cost << std::endl;
  std::cerr << "total_cost_bytes = " << *cost << std::endl;

  double over_ideal_percent =
      (((double)*cost) / ((double)*ideal_cost) * 100.0) - 100.0;
  std::cerr << "%_extra_over_ideal = " << over_ideal_percent << std::endl;
  return 0;
}

static int AnalysisWithFrequency(hb_face_t* font,
                                 const GlyphSegmentation& segmentation) {
  auto freq_data =
      GetFrequencyData(absl::GetFlag(FLAGS_frequency_data_file), {});
  if (!freq_data.ok()) {
    std::cerr << "Failed to load codepoint frequencies: " << freq_data.status()
              << std::endl;
    return -1;
  }

  BigramProbabilityCalculator calculator(std::move(*freq_data));

  ClosureGlyphSegmenter segmenter;
  auto cost = segmenter.TotalCost(font, segmentation, calculator);
  if (!cost.ok()) {
    std::cerr << "Failed to compute cost of segmentation. " << cost.status()
              << std::endl;
    return -1;
  }

  std::cerr << "non_ift_cost_bytes = " << (uint64_t)cost->cost_for_non_segmented
            << std::endl;
  std::cerr << "total_cost_bytes = " << (uint64_t)cost->total_cost << std::endl;
  std::cerr << "ideal_cost_bytes = " << (uint64_t)cost->ideal_cost << std::endl;

  return 0;
}

int main(int argc, char** argv) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  auto font = LoadFont(absl::GetFlag(FLAGS_input_font).c_str());
  if (!font.ok()) {
    std::cerr << "Failed to load input font: " << font.status() << std::endl;
    return -1;
  }

  std::vector<uint32_t> init_codepoints;
  if (!absl::GetFlag(FLAGS_initial_codepoints_file).empty()) {
    auto result = util::LoadCodepointsOrdered(
        absl::GetFlag(FLAGS_initial_codepoints_file).c_str());
    if (!result.ok()) {
      std::cerr << "Failed to load initial codepoints file: " << result.status()
                << std::endl;
      return -1;
    }
    for (const auto& cp : *result) {
      init_codepoints.push_back(cp.codepoint);
    }
  }
  SubsetDefinition init_segment;
  for (hb_codepoint_t cp : init_codepoints) {
    init_segment.codepoints.insert(cp);
  }

  auto codepoints =
      TargetCodepoints(font->get(), absl::GetFlag(FLAGS_codepoints_file),
                       init_segment.codepoints);
  if (!codepoints.ok()) {
    std::cerr << "Failed to load codepoints file: " << codepoints.status()
              << std::endl;
    return -1;
  }

  auto groups =
      GroupCodepoints(*codepoints, absl::GetFlag(FLAGS_number_of_segments));
  if (!groups.ok()) {
    std::cerr << "Failed to generate segment groupings: " << codepoints.status()
              << std::endl;
    return -1;
  }

  for (const auto& tag : absl::GetFlag(FLAGS_optional_feature_tags)) {
    SubsetDefinition s;
    s.feature_tags = {FontHelper::ToTag(tag)};
    groups->push_back(s);
  }

  MergeStrategy merge_strategy =
      MergeStrategy::Heuristic(absl::GetFlag(FLAGS_min_patch_size_bytes),
                               absl::GetFlag(FLAGS_max_patch_size_bytes));
  if (FrequenciesAreRequired()) {
    auto freq_data =
        GetFrequencyData(absl::GetFlag(FLAGS_frequency_data_file), *codepoints);
    if (!freq_data.ok()) {
      std::cerr << "Failed to load codepoint frequency data: "
                << freq_data.status();
    }

    MergingStrategy requested_strategy = absl::GetFlag(FLAGS_merging_strategy);
    auto r = (requested_strategy == COST)
                 ? MergeStrategy::CostBased(
                       std::move(*freq_data),
                       absl::GetFlag(FLAGS_network_overhead_cost))
                 : MergeStrategy::BigramCostBased(
                       std::move(*freq_data),
                       absl::GetFlag(FLAGS_network_overhead_cost));
    if (!r.ok()) {
      std::cerr << "Failed to initialize merging strategy: " << r.status()
                << std::endl;
      return -1;
    }
    merge_strategy = std::move(*r);

    merge_strategy.SetOptimizationCutoffFraction(
        absl::GetFlag(FLAGS_optimization_cutoff_fraction));
  }

  ClosureGlyphSegmenter segmenter;
  auto result = segmenter.CodepointToGlyphSegments(
      font->get(), init_segment, *groups, std::move(merge_strategy));
  if (!result.ok()) {
    std::cerr << result.status() << std::endl;
    return -1;
  }

  if (absl::GetFlag(FLAGS_output_segmentation_plan)) {
    SegmentationPlan plan = result->ToSegmentationPlanProto();
    if (!absl::GetFlag(FLAGS_include_initial_codepoints_in_config)) {
      // Requested to not include init codepoints in the generated config.
      plan.clear_initial_codepoints();
    }

    // TODO(garretrieger): assign a basic (single segment) table keyed config.
    // Later on the input to this util should include information on how the
    // segments should be grouped together for the table keyed portion of the
    // font.
    std::string config_string;
    TextFormat::PrintToString(plan, &config_string);
    std::cout << config_string;
  } else {
    // No config requested, just output a simplified plain text representation
    // of the segmentation.
    std::cout << result->ToString() << std::endl;
  }

  if (!absl::GetFlag(FLAGS_output_segmentation_analysis)) {
    return 0;
  }

  std::cerr << ">> Analysis" << std::endl;
  if (FrequenciesAreRequired()) {
    return AnalysisWithFrequency(font->get(), *result);
  } else {
    return NonFrequencyAnalysis(font->get(), *result);
  }
}
