#include <google/protobuf/text_format.h>

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
#include "common/font_data.h"
#include "hb.h"
#include "ift/encoder/encoder.h"
#include "util/encoder_config.pb.h"

/*
 * Utility that converts a standard font file into an IFT font file following a
 * supplied config.
 *
 * Configuration is provided as a textproto file following the
 * encoder_config.proto schema.
 */

#define TRYV(...)              \
  do {                         \
    auto res = (__VA_ARGS__);  \
    if (!res.ok()) return res; \
  } while (false)

#define TRY(...)                                   \
  ({                                               \
    auto res = (__VA_ARGS__);                      \
    if (!res.ok()) return std::move(res).status(); \
    std::move(*res);                               \
  })

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
using ift::encoder::Encoder;

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

int write_output(const Encoder& encoder, const FontData& base_font) {
  std::string output_path = absl::GetFlag(FLAGS_output_path);
  std::string output_font = absl::GetFlag(FLAGS_output_font);

  std::cerr << "  Writing init font: " << StrCat(output_path, "/", output_font)
            << std::endl;
  auto sc = write_file(StrCat(output_path, "/", output_font), base_font);
  if (!sc.ok()) {
    std::cerr << sc.message() << std::endl;
    return -1;
  }

  for (const auto& p : encoder.Patches()) {
    write_patch(p.first, p.second);
  }

  return 0;
}

template <typename T>
flat_hash_set<uint32_t> values(T proto_set) {
  flat_hash_set<uint32_t> result;
  for (uint32_t v : proto_set.values()) {
    result.insert(v);
  }
  return result;
}

template <typename T>
btree_set<hb_tag_t> tag_values(T proto_set) {
  btree_set<hb_tag_t> result;
  for (const auto& tag : proto_set.values()) {
    result.insert(FontHelper::ToTag(tag));
  }
  return result;
}

Status ConfigureEncoder(EncoderConfig config, Encoder& encoder) {
  // First configure the glyph keyed segments, including features deps
  for (const auto& [id, gids] : config.glyph_segments()) {
    TRYV(encoder.AddGlyphDataSegment(id, values(gids)));
  }

  for (const auto& [id, dep] : config.glyph_patch_dependencies()) {
    if (dep.required_patches().values_size() != 1 ||
        dep.required_features().values_size() != 1) {
      return absl::UnimplementedError(
          "Deps with more than one feature or segment aren't supported yet.");
    }

    uint32_t required_segment_id = dep.required_patches().values().at(0);
    hb_tag_t tag = FontHelper::ToTag(dep.required_features().values().at(0));
    TRYV(encoder.AddFeatureDependency(required_segment_id, id, tag));
  }

  // Initial subset definition
  auto init_codepoints = values(config.initial_codepoints());
  auto init_features = tag_values(config.initial_features());
  auto init_segments = values(config.initial_glyph_patches());
  // TODO(garretrieger): support init design space too

  if ((!init_codepoints.empty() || !init_features.empty()) &&
      init_segments.empty()) {
    Encoder::SubsetDefinition base_subset;
    base_subset.codepoints = init_codepoints;
    base_subset.feature_tags = init_features;
    TRYV(encoder.SetBaseSubsetFromDef(base_subset));
  } else if (init_codepoints.empty() && init_features.empty() &&
             !init_segments.empty()) {
    TRYV(encoder.SetBaseSubsetFromSegments(init_segments));
  } else {
    return absl::UnimplementedError(
        "Setting base subset from both codepoints and glyph patches is not yet "
        "supported.");
  }

  // Next configure the table keyed segments
  for (const auto& codepoints : config.non_glyph_codepoint_segmentation()) {
    encoder.AddNonGlyphDataSegment(values(codepoints));
  }

  for (const auto& features : config.non_glyph_feature_segmentation()) {
    encoder.AddFeatureGroupSegment(tag_values(features));
  }

  // TODO(garretrieger): support design space.

  for (const auto& segments : config.glyph_patch_groupings()) {
    TRYV(encoder.AddNonGlyphSegmentFromGlyphSegments(values(segments)));
  }

  // Lastly graph shape parameters
  if (config.jump_ahead() > 1) {
    encoder.SetJumpAhead(config.jump_ahead());
  }

  // Check for unsupported settings
  if (config.add_everything_else_segments()) {
    return absl::UnimplementedError(
        "add_everything_else_segments is not yet supported.");
  }

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
  auto encoded = encoder.Encode();
  if (!encoded.ok()) {
    std::cerr << "Encoding failed: " << encoded.status() << std::endl;
    return -1;
  }

  std::cout << ">> generating output patches:" << std::endl;
  return write_output(encoder, *encoded);
}
