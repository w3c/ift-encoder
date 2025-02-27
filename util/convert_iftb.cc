#include "util/convert_iftb.h"

#include <cstdint>
#include <cstdlib>
#include <string>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "common/font_helper.h"
#include "hb.h"
#include "util/encoder_config.pb.h"

using absl::btree_map;
using absl::btree_set;
using absl::StatusOr;
using absl::string_view;
using common::FontHelper;

namespace util {

string_view next_token(string_view line, string_view delim, std::string& out) {
  if (line.empty()) {
    out = "";
    return line;
  }

  size_t index = line.find(delim);
  if (index == std::string::npos) {
    out = line;
    return string_view();
  }

  out = line.substr(0, index);
  return line.substr(index + delim.size());
}

btree_set<std::uint32_t> load_chunk_set(string_view line) {
  btree_set<std::uint32_t> result;

  std::string next;
  while (!line.empty()) {
    line = next_token(line, ", ", next);
    result.insert(std::stoi(next));
  }

  return result;
}

btree_map<std::uint32_t, uint32_t> load_gid_map(string_view line) {
  btree_map<std::uint32_t, uint32_t> result;

  std::string next;
  while (!line.empty()) {
    line = next_token(line, ", ", next);

    std::string gid;
    std::string chunk;
    next = next_token(next, ":", gid);
    next = next_token(next, ":", chunk);

    result[std::stoi(gid)] = std::stoi(chunk);
  }

  return result;
}

StatusOr<EncoderConfig> create_config(
    const btree_map<std::uint32_t, uint32_t>& gid_map,
    const btree_set<uint32_t>& loaded_chunks, hb_face_t* face) {
  auto gid_to_unicode = FontHelper::GidToUnicodeMap(face);
  EncoderConfig config;
  // Populate segments in the config. chunks are directly analagous to segments.
  auto segments = config.mutable_glyph_patches();
  for (const auto [gid, chunk] : gid_map) {
    auto cp = gid_to_unicode.find(gid);
    if (cp != gid_to_unicode.end()) {
      Codepoints codepoints;
      auto [it, added] =
          config.mutable_codepoint_sets()->insert(std::pair(chunk, codepoints));
      it->second.add_values(cp->second);
    }

    Glyphs glyphs;
    auto [it, added] = segments->insert(std::pair(chunk, glyphs));
    it->second.add_values(gid);
  }

  // Set up the initial subset, which is specified by loaded_chunks
  for (auto chunk : loaded_chunks) {
    config.mutable_initial_codepoint_sets()->add_values(chunk);
  }

  // Add all non-initial segments to a single non-glyph segment
  // TODO(garretrieger): flag to configure having more than one table keyed
  //                     segment.
  btree_set<uint32_t> non_initial_segments;
  for (const auto [gid, chunk] : gid_map) {
    if (loaded_chunks.contains(chunk)) {
      continue;
    }
    non_initial_segments.insert(chunk);
  }

  CodepointSets* codepoint_sets = config.add_non_glyph_codepoint_set_groups();
  for (auto chunk : non_initial_segments) {
    codepoint_sets->add_values(chunk);
    ActivationConditionProto* condition = config.add_glyph_patch_conditions();
    condition->set_activated_patch(chunk);
    condition->mutable_required_codepoint_sets()->Add()->mutable_values()->Add(
        chunk);
  }

  return config;
}

StatusOr<EncoderConfig> convert_iftb(string_view iftb_dump, hb_face_t* face) {
  btree_map<std::uint32_t, uint32_t> gid_map;
  btree_set<uint32_t> loaded_chunks;

  while (!iftb_dump.empty()) {
    std::string line;
    iftb_dump = next_token(iftb_dump, "\n", line);

    std::string field;
    line = next_token(line, ": ", field);

    fprintf(stderr, ">> %s\n", field.c_str());

    if (field == "gidMap") {
      gid_map = load_gid_map(line);
      continue;
    }

    if (field == "chunkSet indexes") {
      loaded_chunks = load_chunk_set(line);
      continue;
    }
  }

  return create_config(gid_map, loaded_chunks, face);
}

}  // namespace util
