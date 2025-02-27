#include <google/protobuf/text_format.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/font_data.h"
#include "common/hb_set_unique_ptr.h"
#include "common/try.h"
#include "hb.h"
#include "ift/encoder/closure_glyph_segmenter.h"
#include "ift/encoder/condition.h"
#include "ift/encoder/encoder.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/subset_definition.h"
#include "ift/url_template.h"
#include "util/encoder_config.pb.h"

/*
 * Given a code point based segmentation creates an appropriate glyph based
 * segmentation and associated activation conditions that maintain the "closure
 * requirement".
 */

// TODO(garretrieger): have option to output the glyph segmentation plan as an
//                     encoder config proto. Basically two output modes:
//                     - Report
//                     - Config

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Name of the font to convert to IFT.");

ABSL_FLAG(bool, output_encoder_config, false,
          "If set an encoder config representing the determined segmentation "
          "will be output to stdout.");

ABSL_FLAG(bool, output_segmentation_analysis, true,
          "If set an analysis of the segmentation will be output to stderr.");

ABSL_FLAG(
    std::string, codepoints_file, "",
    "Path to a file which defines the desired codepoint based segmentation.");

ABSL_FLAG(uint32_t, number_of_segments, 2,
          "Number of segments to split the input codepoints into.");

ABSL_FLAG(uint32_t, min_patch_size_bytes, 0,
          "The segmenter will try to increase patch sizes to at least this "
          "amount via merging if needed.");

ABSL_FLAG(uint32_t, max_patch_size_bytes, UINT32_MAX,
          "The segmenter will avoid merges which result in patches larger than "
          "this amount.");

using absl::btree_map;
using absl::btree_set;
using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Status;
using absl::StatusOr;
using absl::StrCat;
using common::FontData;
using common::FontHelper;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::hb_set_unique_ptr;
using common::make_hb_blob;
using common::make_hb_set;
using google::protobuf::TextFormat;
using ift::URLTemplate;
using ift::encoder::ClosureGlyphSegmenter;
using ift::encoder::Condition;
using ift::encoder::Encoder;
using ift::encoder::GlyphSegmentation;
using ift::encoder::SubsetDefinition;

StatusOr<FontData> LoadFile(const char* path) {
  hb_blob_unique_ptr blob =
      make_hb_blob(hb_blob_create_from_file_or_fail(path));
  if (!blob.get()) {
    return absl::NotFoundError(StrCat("File ", path, " was not found."));
  }
  return FontData(blob.get());
}

StatusOr<std::vector<uint32_t>> LoadCodepoints(const char* path) {
  std::vector<uint32_t> out;
  std::ifstream in(path);

  if (!in.is_open()) {
    return absl::NotFoundError(
        StrCat("Codepoints file ", path, " was not found."));
  }

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    std::string hex_code;
    std::string description;

    // Extract the hex code and description
    if (iss >> hex_code >> std::ws) {
      if (hex_code.empty() || hex_code.substr(0, 1) == "#") {
        // comment line, skip
        continue;
      } else if (hex_code.substr(0, 2) == "0x") {
        try {
          uint32_t cp = std::stoul(hex_code.substr(2), nullptr, 16);
          out.push_back(cp);
        } catch (const std::out_of_range& oor) {
          return absl::InvalidArgumentError(
              StrCat("Error converting hex code '", hex_code,
                     "' to integer: ", oor.what()));
        } catch (const std::invalid_argument& ia) {
          return absl::InvalidArgumentError(StrCat(
              "Invalid argument for hex code '", hex_code, "': ", ia.what()));
        }
      } else {
        return absl::InvalidArgumentError("Invalid hex code format: " +
                                          hex_code);
      }
    }
  }

  in.close();
  return out;
}

StatusOr<std::vector<uint32_t>> TargetCodepoints(
    hb_face_t* font, const std::string& codepoints_file) {
  hb_set_unique_ptr font_unicodes = make_hb_set();
  hb_face_collect_unicodes(font, font_unicodes.get());
  std::vector<uint32_t> codepoints_filtered;
  if (!codepoints_file.empty()) {
    auto codepoints = TRY(LoadCodepoints(codepoints_file.c_str()));
    for (auto cp : codepoints) {
      if (hb_set_has(font_unicodes.get(), cp)) {
        codepoints_filtered.push_back(cp);
      }
    }
  } else {
    // No codepoints file, just use the full set of codepoints supported by the
    // font.
    hb_codepoint_t cp = HB_SET_VALUE_INVALID;
    while (hb_set_next(font_unicodes.get(), &cp)) {
      codepoints_filtered.push_back(cp);
    }
  }
  return codepoints_filtered;
}

StatusOr<hb_face_unique_ptr> LoadFont(const char* filename) {
  return TRY(LoadFile(filename)).face();
}

constexpr uint32_t NETWORK_REQUEST_BYTE_OVERHEAD = 75;

StatusOr<int> EncodingSize(const GlyphSegmentation* segmentation,
                           const Encoder::Encoding& encoding) {
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
      std::string url =
          URLTemplate::PatchToUrl("1_{id}.gk", condition.activated());

      int type =
          condition.IsExclusive() ? 0 : (!condition.IsFallback() ? 1 : 2);
      patch_id_to_url[condition.activated()] = std::pair(url, type);
    }

    for (const auto& [id, pair] : patch_id_to_url) {
      const std::string& url = pair.first;
      int type = pair.second;
      auto url_size = url_to_size.find(url);
      if (url_size == url_to_size.end()) {
        return absl::InternalError("URL is missing.");
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
  btree_set<uint32_t> glyphs;
  for (const auto& [id, glyph_set] : segmentation.GidSegments()) {
    glyphs.insert(glyph_set.begin(), glyph_set.end());
  }

  uint32_t glyphs_per_patch = glyphs.size() / number_input_segments;
  uint32_t remainder_glyphs = glyphs.size() % number_input_segments;

  Encoder encoder;
  encoder.SetFace(font);

  flat_hash_set<uint32_t> all_unicodes;

  TRYV(encoder.SetBaseSubset(flat_hash_set<uint32_t>{}));

  auto glyphs_it = glyphs.begin();
  for (uint32_t i = 0; i < number_input_segments; i++) {
    auto begin = glyphs_it;
    glyphs_it = std::next(glyphs_it, glyphs_per_patch);
    if (remainder_glyphs > 0) {
      glyphs_it++;
      remainder_glyphs--;
    }

    btree_set<uint32_t> gids;
    gids.insert(begin, glyphs_it);
    auto unicodes = FontHelper::GidsToUnicodes(font, gids);

    TRYV(encoder.AddGlyphDataPatch(i, gids));
    all_unicodes.insert(unicodes.begin(), unicodes.end());

    TRYV(encoder.AddGlyphDataPatchCondition(
        Condition::SimpleCondition(SubsetDefinition::Codepoints(unicodes), i)));
  }

  encoder.AddNonGlyphDataSegment(all_unicodes);

  auto encoding = TRY(encoder.Encode());
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
  Encoder encoder;
  encoder.SetFace(font);

  flat_hash_set<uint32_t> all_segments;

  TRYV(encoder.SetBaseSubset(flat_hash_set<uint32_t>{}));

  for (const auto& [id, glyph_set] : segmentation.GidSegments()) {
    btree_set<uint32_t> s;
    s.insert(glyph_set.begin(), glyph_set.end());
    TRYV(encoder.AddGlyphDataPatch(id, s));
    all_segments.insert(id);
  }

  btree_set<uint32_t> all_codepoints;
  for (const auto& s : segmentation.Segments()) {
    all_codepoints.insert(s.begin(), s.end());
  }
  encoder.AddNonGlyphDataSegment(all_codepoints);

  std::vector<GlyphSegmentation::ActivationCondition> conditions;
  for (const auto& c : segmentation.Conditions()) {
    conditions.push_back(c);
  }

  flat_hash_map<uint32_t, flat_hash_set<uint32_t>> segments;
  uint32_t i = 0;
  for (const auto& s : segmentation.Segments()) {
    segments[i++].insert(s.begin(), s.end());
  }

  auto entries = TRY(GlyphSegmentation::ActivationConditionsToConditionEntries(
      conditions, segments));
  for (const auto& e : entries) {
    TRYV(encoder.AddGlyphDataPatchCondition(e));
  }

  auto encoding = TRY(encoder.Encode());

  return EncodingSize(&segmentation, encoding);
}

std::vector<flat_hash_set<uint32_t>> GroupCodepoints(
    std::vector<uint32_t> codepoints, uint32_t number_of_segments) {
  uint32_t per_group = codepoints.size() / number_of_segments;
  uint32_t remainder = codepoints.size() % number_of_segments;

  std::vector<flat_hash_set<uint32_t>> out;
  auto end = codepoints.begin();
  for (uint32_t i = 0; i < number_of_segments; i++) {
    auto start = end;
    end = std::next(end, per_group);
    if (remainder > 0) {
      end++;
      remainder--;
    }

    flat_hash_set<uint32_t> group;
    btree_set<uint32_t> sorted_group;
    group.insert(start, end);
    out.push_back(group);
  }

  return out;
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

  auto codepoints =
      TargetCodepoints(font->get(), absl::GetFlag(FLAGS_codepoints_file));
  if (!codepoints.ok()) {
    std::cerr << "Failed to load codepoints file: " << codepoints.status()
              << std::endl;
    return -1;
  }

  auto groups =
      GroupCodepoints(*codepoints, absl::GetFlag(FLAGS_number_of_segments));

  ClosureGlyphSegmenter segmenter;
  auto result = segmenter.CodepointToGlyphSegments(
      font->get(), {}, groups, absl::GetFlag(FLAGS_min_patch_size_bytes),
      absl::GetFlag(FLAGS_max_patch_size_bytes));
  if (!result.ok()) {
    std::cerr << result.status() << std::endl;
    return -1;
  }

  if (absl::GetFlag(FLAGS_output_encoder_config)) {
    EncoderConfig config = result->ToConfigProto();
    // TODO(garretrieger): assign a basic (single segment) table keyed config.
    // Later on the input to this util should include information on how the
    // segments should be grouped together for the table keyed portion of the
    // font.
    std::string config_string;
    TextFormat::PrintToString(config, &config_string);
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
  auto cost = SegmentationSize(font->get(), *result);
  if (!cost.ok()) {
    std::cerr << "Failed to compute segmentation cost: " << cost.status()
              << std::endl;
    return -1;
  }
  auto ideal_cost =
      IdealSegmentationSize(font->get(), *result, NumExclusivePatches(*result));
  if (!ideal_cost.ok()) {
    std::cerr << "Failed to compute ideal segmentation cost: " << cost.status()
              << std::endl;
    return -1;
  }

  std::cerr << std::endl;
  std::cerr << "glyphs_in_fallback = " << result->UnmappedGlyphs().size()
            << std::endl;
  std::cerr << "ideal_cost_bytes = " << *ideal_cost << std::endl;
  std::cerr << "total_cost_bytes = " << *cost << std::endl;

  double over_ideal_percent =
      (((double)*cost) / ((double)*ideal_cost) * 100.0) - 100.0;
  std::cout << "%_extra_over_ideal = " << over_ideal_percent << std::endl;

  return 0;
}
