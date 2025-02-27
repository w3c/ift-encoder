#include <google/protobuf/text_format.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/axis_range.h"
#include "common/font_data.h"
#include "common/try.h"
#include "hb.h"
#include "ift/encoder/condition.h"
#include "ift/encoder/encoder.h"
#include "ift/encoder/glyph_segmentation.h"
#include "ift/encoder/subset_definition.h"
#include "util/encoder_config.pb.h"

/*
 * Utility that converts a standard font file into an IFT font file following a
 * supplied config.
 *
 * Configuration is provided as a textproto file following the
 * encoder_config.proto schema.
 */

ABSL_FLAG(std::string, input_font, "in.ttf",
          "Name of the font to convert to IFT.");

ABSL_FLAG(std::string, config, "",
          "Path to a config file which is a textproto following the "
          "encoder_config.proto schema.");

ABSL_FLAG(std::string, output_path, "./",
          "Path to write output files under (base font and patches).");

ABSL_FLAG(std::string, output_font, "out.ttf",
          "Name of the outputted base font.");

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
using common::make_hb_blob;
using ift::encoder::Condition;
using ift::encoder::design_space_t;
using ift::encoder::Encoder;
using ift::encoder::GlyphSegmentation;
using ift::encoder::SubsetDefinition;

// TODO(garretrieger): add check that all glyph patches have at least one
// activation condition.
// TODO(garretrieger): add check that warns when not all parts of the input font
// are reachable in the generated encoding.
//                     (all glyph ids covered by a patch, all codepoints, etc,
//                     covered by non glyph segments).

StatusOr<FontData> load_file(const char* path) {
  hb_blob_unique_ptr blob =
      make_hb_blob(hb_blob_create_from_file_or_fail(path));
  if (!blob.get()) {
    return absl::NotFoundError(StrCat("File ", path, " was not found."));
  }
  return FontData(blob.get());
}

StatusOr<hb_face_unique_ptr> load_font(const char* filename) {
  return TRY(load_file(filename)).face();
}

Status write_file(const std::string& name, const FontData& data) {
  std::ofstream output(name,
                       std::ios::out | std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return absl::NotFoundError(StrCat("File ", name, " was not found."));
  }
  output.write(data.data(), data.size());
  if (output.bad()) {
    output.close();
    return absl::InternalError(StrCat("Failed to write to ", name, "."));
  }

  output.close();
  return absl::OkStatus();
}

void write_patch(const std::string& url, const FontData& patch) {
  std::string output_path = absl::GetFlag(FLAGS_output_path);
  std::cerr << "  Writing patch: " << StrCat(output_path, "/", url)
            << std::endl;
  auto sc = write_file(StrCat(output_path, "/", url), patch);
  if (!sc.ok()) {
    std::cerr << sc.message() << std::endl;
    exit(-1);
  }
}

int write_output(const Encoder::Encoding& encoding) {
  std::string output_path = absl::GetFlag(FLAGS_output_path);
  std::string output_font = absl::GetFlag(FLAGS_output_font);

  std::cerr << "  Writing init font: " << StrCat(output_path, "/", output_font)
            << std::endl;
  auto sc =
      write_file(StrCat(output_path, "/", output_font), encoding.init_font);
  if (!sc.ok()) {
    std::cerr << sc.message() << std::endl;
    return -1;
  }

  for (const auto& p : encoding.patches) {
    write_patch(p.first, p.second);
  }

  return 0;
}

template <typename T>
btree_set<uint32_t> values(const T& proto_set) {
  btree_set<uint32_t> result;
  for (uint32_t v : proto_set.values()) {
    result.insert(v);
  }
  return result;
}

template <typename T>
btree_set<hb_tag_t> tag_values(const T& proto_set) {
  btree_set<hb_tag_t> result;
  for (const auto& tag : proto_set.values()) {
    result.insert(FontHelper::ToTag(tag));
  }
  return result;
}

StatusOr<design_space_t> to_design_space(const DesignSpace& proto) {
  design_space_t result;
  for (const auto& [tag_str, range_proto] : proto.ranges()) {
    auto range =
        TRY(common::AxisRange::Range(range_proto.start(), range_proto.end()));
    result[FontHelper::ToTag(tag_str)] = range;
  }
  return result;
}

GlyphSegmentation::ActivationCondition FromProto(
    const ActivationConditionProto& condition) {
  // TODO(garretrieger): once glyph segmentation activation conditions can
  // support features copy those here.
  std::vector<btree_set<uint32_t>> groups;
  for (const auto& group : condition.required_codepoint_sets()) {
    btree_set<uint32_t> set;
    set.insert(group.values().begin(), group.values().end());
    groups.push_back(set);
  }

  return GlyphSegmentation::ActivationCondition::composite_condition(
      groups, condition.activated_patch());
}

Status ConfigureEncoder(EncoderConfig config, Encoder& encoder) {
  // First configure the glyph keyed segments, including features deps
  for (const auto& [id, gids] : config.glyph_patches()) {
    TRYV(encoder.AddGlyphDataPatch(id, values(gids)));
  }

  std::vector<GlyphSegmentation::ActivationCondition> activation_conditions;
  for (const auto& c : config.glyph_patch_conditions()) {
    if (c.required_features().values_size() > 0) {
      return absl::UnimplementedError(
          "Conditions with more features aren't supported yet.");
    }

    activation_conditions.push_back(FromProto(c));
  }

  flat_hash_map<uint32_t, flat_hash_set<uint32_t>> codepoint_sets;
  for (const auto& [id, set] : config.codepoint_sets()) {
    codepoint_sets[id].insert(set.values().begin(), set.values().end());
  }

  auto condition_entries =
      TRY(GlyphSegmentation::ActivationConditionsToConditionEntries(
          activation_conditions, codepoint_sets));
  for (const auto& entry : condition_entries) {
    TRYV(encoder.AddGlyphDataPatchCondition(entry));
  }

  // Initial subset definition
  auto init_codepoints = values(config.initial_codepoints());
  auto init_features = tag_values(config.initial_features());
  auto init_codepoint_sets = values(config.initial_codepoint_sets());
  auto init_design_space = TRY(to_design_space(config.initial_design_space()));

  SubsetDefinition base_subset;
  base_subset.codepoints.insert(init_codepoints.begin(), init_codepoints.end());
  for (const auto set_id : init_codepoint_sets) {
    auto set = codepoint_sets.find(set_id);
    if (set == codepoint_sets.end()) {
      return absl::InvalidArgumentError(
          StrCat("Codepoint set id, ", set_id, ", not found."));
    }

    base_subset.codepoints.insert(set->second.begin(), set->second.end());
  }
  base_subset.feature_tags = init_features;
  base_subset.design_space = init_design_space;
  TRYV(encoder.SetBaseSubsetFromDef(base_subset));

  // Next configure the table keyed segments
  for (const auto& codepoints : config.non_glyph_codepoint_segmentation()) {
    encoder.AddNonGlyphDataSegment(values(codepoints));
  }

  for (const auto& features : config.non_glyph_feature_segmentation()) {
    encoder.AddFeatureGroupSegment(tag_values(features));
  }

  for (const auto& design_space_proto :
       config.non_glyph_design_space_segmentation()) {
    auto design_space = TRY(to_design_space(design_space_proto));
    encoder.AddDesignSpaceSegment(design_space);
  }

  for (const auto& sets : config.non_glyph_codepoint_set_groups()) {
    flat_hash_set<uint32_t> codepoints;
    for (const auto& set_id : sets.values()) {
      auto set = codepoint_sets.find(set_id);
      if (set == codepoint_sets.end()) {
        return absl::InvalidArgumentError(
            StrCat("Codepoint set id, ", set_id, ", not found."));
      }
      codepoints.insert(set->second.begin(), set->second.end());
    }
    encoder.AddNonGlyphDataSegment(codepoints);
  }

  // Lastly graph shape parameters
  if (config.jump_ahead() > 1) {
    encoder.SetJumpAhead(config.jump_ahead());
  }

  // Check for unsupported settings
  if (config.include_all_segment_patches()) {
    return absl::UnimplementedError(
        "include_all_segment_patches is not yet supported.");
  }

  if (config.max_depth() > 0) {
    return absl::UnimplementedError("max_depth is not yet supported.");
  }

  return absl::OkStatus();
}

int main(int argc, char** argv) {
  auto args = absl::ParseCommandLine(argc, argv);

  auto config_text = load_file(absl::GetFlag(FLAGS_config).c_str());
  if (!config_text.ok()) {
    std::cerr << "Failed to load config file: " << config_text.status()
              << std::endl;
    return -1;
  }

  EncoderConfig config;
  if (!google::protobuf::TextFormat::ParseFromString(config_text->str(),
                                                     &config)) {
    std::cerr << "Failed to parse input config." << std::endl;
    return -1;
  }

  auto font = load_font(absl::GetFlag(FLAGS_input_font).c_str());
  if (!font.ok()) {
    std::cerr << "Failed to load input font: " << font.status() << std::endl;
    return -1;
  }

  Encoder encoder;
  encoder.SetFace(font->get());

  auto sc = ConfigureEncoder(config, encoder);
  if (!sc.ok()) {
    std::cerr << "Failed to apply configuration to the encoder: " << sc
              << std::endl;
    return -1;
  }

  std::cout << ">> encoding:" << std::endl;
  auto encoding = encoder.Encode();
  if (!encoding.ok()) {
    std::cerr << "Encoding failed: " << encoding.status() << std::endl;
    return -1;
  }

  std::cout << ">> generating output patches:" << std::endl;
  return write_output(*encoding);
}
