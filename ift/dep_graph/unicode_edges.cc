#include "ift/dep_graph/unicode_edges.h"

#include <fstream>
#include "absl/strings/str_cat.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "ift/common/font_helper.h"
#include "ift/common/int_set.h"
#include "ift/common/try.h"
#include "tools/cpp/runfiles/runfiles.h"
#include "ift/common/font_data.h"
#include "ift/common/hb_set_unique_ptr.h"

using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using bazel::tools::cpp::runfiles::Runfiles;
using ift::common::CodepointSet;
using ift::common::FontHelper;
using ift::common::make_hb_set;
using ift::common::make_hb_font;
using ift::common::hb_set_unique_ptr;
using ift::common::hb_font_unique_ptr;

namespace ift::dep_graph {

absl::Status ParseDerivedNormalizationProps(
    const std::string& path,
    CodepointSet& full_composition_exclusions) {
  std::ifstream props_file(path);
  if (!props_file.is_open()) {
    return absl::NotFoundError(absl::StrCat("Failed to open ", path));
  }

  bool started = false;
  std::string line;
  while (std::getline(props_file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t semi = line.find(';');
    if (semi == std::string::npos) {
      continue;
    }

    absl::string_view line_view = line;
    absl::string_view prop = line_view.substr(semi + 1);
    size_t first = prop.find_first_not_of(" \t");
    size_t last = prop.find_last_not_of(" \t");
    if (first == std::string::npos || last == std::string::npos) {
      continue;
    }
    prop = prop.substr(first, last - first + 1);

    if (prop.find("Full_Composition_Exclusion") == std::string::npos) {
      if (!started) {
        continue;
      } else {
        break;
      }
    }
    started = true;

    absl::string_view range = line_view.substr(0, semi);
    size_t dotdot = range.find("..");
    if (dotdot != std::string::npos) {
      hb_codepoint_t start = 0, end = 0;
      if (!absl::SimpleHexAtoi(range.substr(0, dotdot), &start) ||
          !absl::SimpleHexAtoi(range.substr(dotdot + 2), &end)) {
        return absl::InternalError(
          absl::StrCat(
          "Failed to parse unicode range in DerivedNormalizationProps.txt: ", range));
      }
      full_composition_exclusions.insert_range(start, end);
    } else {
      hb_codepoint_t u = 0;
      if (!absl::SimpleHexAtoi(range, &u)) {
        return absl::InternalError(absl::StrCat("Failed to parse unicode value in DerivedNormalizationProps.txt: ", range));
      }
      full_composition_exclusions.insert(u);
    }
  }
  return absl::OkStatus();
}

Status ParseUnicodeData(
    const std::string& path,
    const CodepointSet& unicodes,
    const CodepointSet& full_composition_exclusions,
    UnicodeEdges& result) {

  std::ifstream data_file(path);
  if (!data_file.is_open()) {
    return absl::NotFoundError(absl::StrCat("Failed to open ", path));
  }
  std::string line;
  while (std::getline(data_file, line)) {
    if (line.empty()) {
      continue;
    }

    absl::string_view line_view = line;
    std::vector<absl::string_view> fields;
    fields.reserve(6);
    size_t pos = 0;
    while (pos < line.size() && fields.size() < 6) {
      size_t next_semi = line.find(';', pos);
      if (next_semi == std::string::npos) {
        fields.push_back(line_view.substr(pos));
        break;
      }
      fields.push_back(line_view.substr(pos, next_semi - pos));
      pos = next_semi + 1;
    }

    if (fields.size() != 6) {
      continue;
    }

    hb_codepoint_t base = 0;
    if (!absl::SimpleHexAtoi(fields[0], &base)) {
      return absl::InternalError(absl::StrCat("Failed to parse unicode value in UnicodeData.txt: ", fields[0]));
    }
    if (!unicodes.contains(base)) {
      continue;
    }

    absl::string_view decomp = fields[5];

    if (decomp.empty() || decomp[0] == '<') {
      continue;
    }

    CodepointSet decomp_chars;
    size_t space_pos = 0;
    bool keep = true;
    while (space_pos < decomp.size()) {
      size_t next_space = decomp.find(' ', space_pos);
      absl::string_view next = decomp.substr(space_pos);
      if (next_space != std::string::npos) {
        next = decomp.substr(space_pos, next_space - space_pos);
      }
      hb_codepoint_t u = 0;
      if (!absl::SimpleHexAtoi(next, &u)) {
        return absl::InternalError(absl::StrCat("Failed to parse unicode value in UnicodeData.txt: ", next));
      }
      if (!unicodes.contains(u)) {
        keep = false;
        break;
      }
      decomp_chars.insert(u);
      if (next_space == std::string::npos) {
        break;
      }
      space_pos = next_space + 1;
    }

    if (!keep) {
      continue;
    }

    result.decomposition[base].union_set(decomp_chars);

    if (decomp_chars.size() == 2 && !full_composition_exclusions.contains(base)) {
      hb_codepoint_t d0 = *decomp_chars.begin();
      hb_codepoint_t d1 = *(++decomp_chars.begin());
      result.composition[d0].push_back(UnicodeConjunctiveEdge{d1, base});
      result.composition[d1].push_back(UnicodeConjunctiveEdge{d0, base});
    }
  }
  return absl::OkStatus();
}

flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> UnicodeEdges::UnicodeToGid(
    hb_face_t* face) {
  flat_hash_map<hb_codepoint_t, encoder::glyph_id_t> out;
  hb_map_t* unicode_to_gid = hb_map_create();
  hb_face_collect_nominal_glyph_mapping(face, unicode_to_gid, nullptr);
  int index = -1;
  uint32_t cp = HB_MAP_VALUE_INVALID;
  uint32_t gid = HB_MAP_VALUE_INVALID;
  while (hb_map_next(unicode_to_gid, &index, &cp, &gid)) {
    out[cp] = gid;
  }
  hb_map_destroy(unicode_to_gid);
  return out;
}

void UnicodeEdges::ComputeUVSEdges(hb_face_t* face, const flat_hash_map<hb_codepoint_t, encoder::glyph_id_t>& unicode_to_gid, UnicodeEdges& result) {
  hb_set_unique_ptr vs_unicodes_hb = make_hb_set();
  hb_face_collect_variation_selectors(face, vs_unicodes_hb.get());
  CodepointSet vs_unicodes(vs_unicodes_hb.get());

  hb_font_unique_ptr font = make_hb_font(hb_font_create(face));

  for (auto [u, gid] : unicode_to_gid) {
    for (auto vs : vs_unicodes) {
      hb_codepoint_t dep_gid;
      if (!hb_font_get_variation_glyph(font.get(), u, vs, &dep_gid)) {
        continue;
      }

      if (dep_gid == gid) {
        // default mapping, gid isn't changed so we can ignore.
        continue;
      }

      result.variation_selector[u].push_back(VariationSelectorEdge{
          .unicode = vs,
          .gid = dep_gid,
      });
      result.variation_selector[vs].push_back(VariationSelectorEdge{
          .unicode = u,
          .gid = dep_gid,
      });
      result.gid_to_vs[dep_gid].insert(vs);
    }
  }
}

StatusOr<UnicodeEdges> UnicodeEdges::ComputeUnicodeDependencyEdges(hb_face_t* face) {

  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create("", &error));

  if (!runfiles) {
    return absl::InternalError(absl::StrCat("Failed to create runfiles: ", error));
  }

  std::string unicode_data_path = runfiles->Rlocation("+_repo_rules2+unicode_data/file/downloaded");
  std::string derived_props_path = runfiles->Rlocation("+_repo_rules2+derived_normalization_props/file/downloaded");

  if (unicode_data_path.empty()) {
    return absl::NotFoundError("Failed to find UnicodeData.txt via runfiles");
  }
  if (derived_props_path.empty()) {
    return absl::NotFoundError("Failed to find DerivedNormalizationProps.txt via runfiles");
  }

  CodepointSet unicodes = FontHelper::ToCodepointsSet(face);
  UnicodeEdges result;
  CodepointSet full_composition_exclusions;
  TRYV(ParseDerivedNormalizationProps(derived_props_path, full_composition_exclusions));
  TRYV(ParseUnicodeData(unicode_data_path, unicodes, full_composition_exclusions, result));

  // Compute UVS edges
  result.unicode_to_gid = UnicodeToGid(face);
  ComputeUVSEdges(face, result.unicode_to_gid, result);

  return result;
}

}  // namespace ift::dep_graph
