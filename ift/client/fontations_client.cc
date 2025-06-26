#include "ift/client/fontations_client.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <sstream>

#include "absl/status/status.h"
#include "common/axis_range.h"
#include "common/font_data.h"
#include "common/int_set.h"
#include "ift/encoder/compiler.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using common::AxisRange;
using common::FontData;
using common::IntSet;
using common::make_hb_blob;
using common::make_hb_face;
using ift::encoder::Compiler;

namespace ift::client {

Status ToFile(const FontData& data, const char* path) {
  FILE* f = fopen(path, "wb");
  if (!f) {
    return absl::InternalError("Unable to open file for output.");
  }

  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
  return absl::OkStatus();
}

void ParseGraph(const std::string& text, graph& out) {
  std::stringstream ss(text);

  std::string line;
  while (getline(ss, line)) {
    std::stringstream line_ss(line);
    std::string node;
    if (!getline(line_ss, node, ';')) {
      continue;
    }

    auto& edges = out[node];

    std::string edge;
    while (getline(line_ss, edge, ';')) {
      edges.insert(edge);
    }
  }
}

void ParseFetched(const std::string& text, btree_set<std::string>& uris_out) {
  std::stringstream ss(text);
  std::string marker("    Fetching ");

  std::string line;
  while (getline(ss, line)) {
    if (line.substr(0, marker.size()) == marker) {
      std::string uri(line.substr(marker.size()));
      uris_out.insert(uri);
    }
  }
}

StatusOr<std::string> WriteFontToDisk(const Compiler::Encoding& encoding) {
  char template_str[] = "fontations_client_XXXXXX";
  const char* temp_dir = mkdtemp(template_str);

  if (!temp_dir) {
    return absl::InternalError("Failed to create temp working directory.");
  }

  std::string font_path = absl::StrCat(temp_dir, "/font.ttf");
  auto sc = ToFile(encoding.init_font, font_path.c_str());
  if (!sc.ok()) {
    return sc;
  }

  for (auto& p : encoding.patches) {
    auto& path = p.first;
    auto& data = p.second;
    std::string full_path = absl::StrCat(temp_dir, "/", path);
    auto sc = ToFile(data, full_path.c_str());
    if (!sc.ok()) {
      return sc;
    }
  }

  return font_path;
}

StatusOr<std::string> Exec(const char* cmd) {
  std::array<char, 128> buffer;
  std::string result;
  FILE* pipe = popen(cmd, "r");
  if (!pipe) {
    return absl::InternalError("Unable to start process.");
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
         nullptr) {
    result += buffer.data();
  }
  if (pclose(pipe)) {
    return absl::InternalError("exec command failed.");
  }
  return result;
}

Status ToGraph(const Compiler::Encoding& encoding, graph& out,
               bool include_patch_paths) {
  auto font_path = WriteFontToDisk(encoding);
  if (!font_path.ok()) {
    return font_path.status();
  }

  std::string command = absl::StrCat(
      "${TEST_SRCDIR}/+_repo_rules+fontations/ift_graph --font=", *font_path);
  if (include_patch_paths) {
    command = absl::StrCat(command, " --include-patch-paths");
  }

  auto r = Exec(command.c_str());
  if (!r.ok()) {
    return r.status();
  }

  ParseGraph(*r, out);

  return absl::OkStatus();
}

StatusOr<FontData> ExtendWithDesignSpace(
    const Compiler::Encoding& encoding, const IntSet& codepoints,
    const btree_set<hb_tag_t>& feature_tags,
    const flat_hash_map<hb_tag_t, AxisRange>& design_space,
    btree_set<std::string>* applied_uris, uint32_t max_round_trips,
    uint32_t max_fetches) {
  auto font_path_str = WriteFontToDisk(encoding);
  if (!font_path_str.ok()) {
    return font_path_str.status();
  }

  std::filesystem::path font_path(*font_path_str);
  std::filesystem::path directory = font_path.parent_path();
  std::filesystem::path output = directory / "out.ttf";

  std::stringstream ss;
  for (uint32_t cp : codepoints) {
    ss << cp << ",";
  }
  std::string unicodes = ss.str();
  if (!unicodes.empty()) {
    unicodes = unicodes.substr(0, unicodes.size() - 1);
  }

  std::stringstream features_ss;
  for (uint32_t tag : feature_tags) {
    char tag_string[5] = {'a', 'a', 'a', 'a', 0};
    snprintf(tag_string, 5, "%c%c%c%c", HB_UNTAG(tag));
    features_ss << tag_string << ",";
  }
  std::string features = features_ss.str();
  if (!features.empty()) {
    features = features.substr(0, features.size() - 1);
  }

  std::stringstream ds_ss;
  for (const auto& [tag, range] : design_space) {
    char tag_string[5] = {'a', 'a', 'a', 'a', 0};
    snprintf(tag_string, 5, "%c%c%c%c", HB_UNTAG(tag));

    ds_ss << tag_string << "@" << range.start();
    if (range.IsRange()) {
      ds_ss << ":" << range.end();
    }
    ds_ss << ",";
  }
  std::string design_space_str = ds_ss.str();
  if (!design_space_str.empty()) {
    design_space_str = design_space_str.substr(0, design_space_str.size() - 1);
  }

  // Run the extension
  std::string command = absl::StrCat(
      "${TEST_SRCDIR}/+_repo_rules+fontations/ift_extend --font=",
      font_path.string(), " --unicodes=\"", unicodes, "\" --design-space=\"",
      design_space_str, "\" --features=\"", features,
      "\" --max-round-trips=", max_round_trips, " --max-fetches=", max_fetches,
      " --output=", output.string());
  auto r = Exec(command.c_str());
  if (!r.ok()) {
    return r.status();
  }

  if (applied_uris) {
    ParseFetched(*r, *applied_uris);
  }

  return FontData(make_hb_blob(hb_blob_create_from_file(output.c_str())));
}

StatusOr<FontData> Extend(const Compiler::Encoding& encoding,
                          const IntSet& codepoints, uint32_t max_round_trips,
                          uint32_t max_fetches) {
  absl::flat_hash_map<hb_tag_t, common::AxisRange> design_space;
  return ExtendWithDesignSpace(encoding, codepoints, {}, design_space, nullptr,
                               max_round_trips, max_fetches);
}

}  // namespace ift::client