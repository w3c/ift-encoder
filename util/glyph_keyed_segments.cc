#include <google/protobuf/text_format.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/font_data.h"
#include "common/hb_set_unique_ptr.h"
#include "common/try.h"
#include "hb.h"
#include "ift/encoder/encoder.h"
#include "ift/encoder/glyph_segmentation.h"

/*
 * Given a code point based segmentation creates an appropriate glyph based
 * segmentation and associated activation conditions that maintain the "closure
 * requirement".
 */

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Name of the font to convert to IFT.");

ABSL_FLAG(
    std::string, codepoints_file, "",
    "Path to a file which defines the desired codepoint based segmentation.");

ABSL_FLAG(uint32_t, number_of_segments, 2,
          "Number of segments to split the input codepoints into.");

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
using ift::encoder::Encoder;
using ift::encoder::GlyphSegmentation;

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

StatusOr<int> EncodingSize(const Encoder::Encoding& encoding) {
  // There are three parts to the cost of a segmentation:
  // - Size of the glyph keyed mapping table.
  // - Total size of all glyph keyed patches
  // - Network overhead (fixed cost per patch).
  auto init_font = encoding.init_font.face();

  uint32_t total_size = 0;
  for (const auto& [url, data] : encoding.patches) {
    if (url.substr(url.size() - 2) == "gk") {
      total_size += data.size() + NETWORK_REQUEST_BYTE_OVERHEAD;
      printf("  patch %s adds %u bytes, %u bytes overhead\n", url.c_str(),
             data.size(), NETWORK_REQUEST_BYTE_OVERHEAD);
    }
  }

  auto iftx =
      FontHelper::TableData(init_font.get(), HB_TAG('I', 'F', 'T', 'X'));
  total_size += iftx.size();
  printf("  mapping table %u bytes\n", iftx.size());

  return total_size;
}

// The "ideal" segmentation is one where if we could ignore the glyph closure
// requirement then the glyphs could be evenly distributed between the desired
// number of input segments. This should minimize overhead.
StatusOr<int> IdealSegmentationSize(hb_face_t* font,
                                    const GlyphSegmentation& segmentation,
                                    uint32_t number_input_segments) {
  printf("IdealSegmentationSize():\n");
  btree_set<uint32_t> glyphs;
  for (const auto& [id, glyph_set] : segmentation.GidSegments()) {
    glyphs.insert(glyph_set.begin(), glyph_set.end());
  }

  uint32_t glyphs_per_patch = glyphs.size() / number_input_segments;
  uint32_t remainder_glyphs = glyphs.size() % number_input_segments;

  Encoder encoder;
  encoder.SetFace(font);

  flat_hash_set<uint32_t> all_segments;

  TRYV(encoder.SetBaseSubset({}));

  auto glyphs_it = glyphs.begin();
  for (uint32_t i = 0; i < number_input_segments; i++) {
    auto begin = glyphs_it;
    glyphs_it = std::next(glyphs_it, glyphs_per_patch);
    if (remainder_glyphs > 0) {
      glyphs_it++;
      remainder_glyphs--;
    }

    flat_hash_set<uint32_t> gids;
    gids.insert(begin, glyphs_it);
    TRYV(encoder.AddGlyphDataSegment(i, gids));
    all_segments.insert(i);
    TRYV(encoder.AddGlyphDataActivationCondition(Encoder::Condition(i)));
  }

  TRYV(encoder.AddNonGlyphSegmentFromGlyphSegments(all_segments));

  auto encoding = TRY(encoder.Encode());
  return EncodingSize(encoding);
}

StatusOr<int> SegmentationSize(hb_face_t* font,
                               const GlyphSegmentation& segmentation) {
  printf("SegmentationSize():\n");
  Encoder encoder;
  encoder.SetFace(font);

  flat_hash_set<uint32_t> all_segments;

  TRYV(encoder.SetBaseSubset({}));

  for (const auto& [id, glyph_set] : segmentation.GidSegments()) {
    flat_hash_set<uint32_t> s;
    s.insert(glyph_set.begin(), glyph_set.end());
    TRYV(encoder.AddGlyphDataSegment(id, s));
    all_segments.insert(id);
  }

  TRYV(encoder.AddNonGlyphSegmentFromGlyphSegments(all_segments));

  for (const auto& c : segmentation.Conditions()) {
    Encoder::Condition condition;
    for (const auto& g : c.conditions()) {
      condition.required_groups.push_back(g);
    }
    condition.activated_segment_id = c.activated();
    TRYV(encoder.AddGlyphDataActivationCondition(condition));
  }

  auto encoding = TRY(encoder.Encode());
  return EncodingSize(encoding);
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
  auto args = absl::ParseCommandLine(argc, argv);

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

  auto result = ift::encoder::GlyphSegmentation::CodepointToGlyphSegments(
      font->get(), {}, groups);
  if (!result.ok()) {
    std::cerr << result.status() << std::endl;
    return -1;
  }

  std::cout << ">> Computed Segmentation" << std::endl;
  std::cout << result->ToString() << std::endl;

  std::cout << ">> Analysis" << std::endl;
  auto cost = SegmentationSize(font->get(), *result);
  if (!cost.ok()) {
    std::cerr << "Failed to compute segmentation cost: " << cost.status()
              << std::endl;
    return -1;
  }
  auto ideal_cost = IdealSegmentationSize(
      font->get(), *result, absl::GetFlag(FLAGS_number_of_segments));
  if (!ideal_cost.ok()) {
    std::cerr << "Failed to compute segmentation cost: " << cost.status()
              << std::endl;
    return -1;
  }

  std::cout << std::endl;
  std::cout << "glyphs_in_fallback = " << result->UnmappedGlyphs().size()
            << std::endl;
  std::cout << "ideal_cost_bytes = " << *ideal_cost << std::endl;
  std::cout << "total_cost_bytes = " << *cost << std::endl;

  double over_ideal_percent =
      (((double)*cost) / ((double)*ideal_cost) * 100.0) - 100.0;
  std::cout << "%_extra_over_ideal = " << over_ideal_percent << std::endl;

  return 0;
}
