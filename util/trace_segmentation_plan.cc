#include <google/protobuf/text_format.h>

#include <sstream>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "common/int_set.h"
#include "util/load_codepoints.h"
#include "util/segmentation_plan.pb.h"

using absl::Status;
using absl::StatusOr;
using common::CodepointSet;
using common::GlyphSet;
using common::IntSet;
using common::SegmentSet;

ABSL_FLAG(std::string, plan, "",
          "File path to a segmentation plan text proto.");

ABSL_FLAG(std::vector<std::string>, gids, {},
          "List of glyphs to dump information from the plan for.");

ABSL_FLAG(std::vector<std::string>, codepoints, {},
          "List of codepoints to dump information from the plan for.");

ABSL_FLAG(std::vector<std::string>, segments, {},
          "List of segments to dump information from the plan for.");

StatusOr<SegmentationPlan> LoadPlan() {
  if (absl::GetFlag(FLAGS_plan).empty()) {
    return absl::InvalidArgumentError("plan must be provided.");
  }

  auto config_text = TRY(util::LoadFile(absl::GetFlag(FLAGS_plan).c_str()));

  SegmentationPlan plan;
  if (!google::protobuf::TextFormat::ParseFromString(config_text.str(),
                                                     &plan)) {
    return absl::InvalidArgumentError("Unable to parse segmentation plan.");
  }

  return plan;
}

GlyphSet LoadGids() {
  std::vector<std::string> gids_strings = absl::GetFlag(FLAGS_gids);
  GlyphSet out;
  for (const auto& gid_str : gids_strings) {
    out.insert(std::stoi(gid_str));
  }
  return out;
}

CodepointSet LoadCodepoints() {
  std::vector<std::string> codepoint_strings = absl::GetFlag(FLAGS_codepoints);
  CodepointSet out;
  for (const auto& cp_str : codepoint_strings) {
    out.insert(std::stoi(cp_str));
  }
  return out;
}

SegmentSet LoadSegments() {
  std::vector<std::string> segment_strings = absl::GetFlag(FLAGS_segments);
  SegmentSet out;
  for (const auto& seg_str : segment_strings) {
    out.insert(std::stoi(seg_str));
  }
  return out;
}

SegmentSet SegmentsFrom(const ActivationConditionProto& condition) {
  SegmentSet out;
  for (const auto& segments : condition.required_segments()) {
    for (unsigned segment : segments.values()) {
      out.insert(segment);
    }
  }
  return out;
}

std::string ConditionToString(const ActivationConditionProto& condition) {
  std::stringstream ss;
  ss << "if ";
  bool first_and = true;
  for (const auto& segments : condition.required_segments()) {
    if (!first_and) {
      ss << " AND ";
    } else {
      first_and = false;
    }

    ss << "(";
    bool first_or = true;
    for (unsigned segment : segments.values()) {
      if (!first_or) {
        ss << " OR ";
      } else {
        first_or = false;
      }
      ss << "s" << segment;
    }
    ss << ")";
  }

  ss << " then p" << condition.activated_patch();

  return ss.str();
}

std::string SegmentToString(const SegmentProto& segment,
                            const CodepointSet& of_interest) {
  CodepointSet codepoints;
  for (unsigned s : segment.codepoints().values()) {
    codepoints.insert(s);
  }
  std::stringstream ss;
  ss << "cps " << codepoints.ToString();
  if (!segment.features().values().empty()) {
    ss << ", features";
    for (const std::string& tag : segment.features().values()) {
      ss << " " << tag;
    }
  }

  codepoints.intersect(of_interest);
  if (!codepoints.empty()) {
    ss << ", of interest " << codepoints.ToString();
  }

  return ss.str();
}

static Status Main(const std::vector<char*> args) {
  SegmentationPlan plan = TRY(LoadPlan());
  GlyphSet target_gids = LoadGids();
  CodepointSet target_codepoints = LoadCodepoints();
  SegmentSet target_segments = LoadSegments();

  for (unsigned cp : plan.initial_codepoints().values()) {
    if (target_codepoints.contains(cp)) {
      std::cout << "Initial font has u" << cp << std::endl;
    }
  }

  for (unsigned gid : plan.initial_glyphs().values()) {
    if (target_gids.contains(gid)) {
      std::cout << "Initial font has g" << gid << std::endl;
    }
  }

  IntSet patch_ids;
  for (const auto& [patch_id, glyphs] : plan.glyph_patches()) {
    for (unsigned gid : glyphs.values()) {
      if (target_gids.contains(gid)) {
        std::cout << "Patch p" << patch_id << " has g" << gid << std::endl;
        patch_ids.insert(patch_id);
      }
    }
  }

  SegmentSet additional_target_segments;
  for (const auto& condition : plan.glyph_patch_conditions()) {
    SegmentSet condition_segments = SegmentsFrom(condition);
    if (patch_ids.contains(condition.activated_patch()) ||
        condition_segments.intersects(target_segments)) {
      std::cout << ConditionToString(condition) << std::endl;
      additional_target_segments.union_set(condition_segments);
    }
  }
  target_segments.union_set(additional_target_segments);

  for (const auto& [index, segment] : plan.segments()) {
    bool output = target_segments.contains(index);
    for (const auto& cp : segment.codepoints().values()) {
      if (target_codepoints.contains(cp)) {
        output = true;
      }
    }

    if (output) {
      std::cout << "s" << index << " = "
                << SegmentToString(segment, target_codepoints) << std::endl;
    }
  }

  return absl::OkStatus();
}

int main(int argc, char** argv) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  auto args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  auto sc = Main(args);
  if (!sc.ok()) {
    std::cerr << "Error: " << sc << std::endl;
    return -1;
  }
  return 0;
}