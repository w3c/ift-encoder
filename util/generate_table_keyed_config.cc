#include <vector>

#include <google/protobuf/text_format.h>

#include "absl/flags/parse.h"
#include "absl/container/btree_set.h"
#include "util/load_codepoints.h"
#include "util/encoder_config.pb.h"

using absl::btree_set;
using google::protobuf::TextFormat;

// TODO(garretrieger): add flag which specifies a font, it will be checked and any codepoints it has which are not
//                     mentioned in a provided codepoint set file should be added as one final segment.

template <typename ProtoType>
ProtoType ToSetProto(const btree_set<uint32_t>& set) {
  ProtoType values;
  for (uint32_t v : set) {
    values.add_values(v);
  }
  return values;
}

/*
 * This utility takes a font + a list of code point subsets and emits an IFT
 * encoder config that will configure the font to be extended by table keyed
 * patches (where each subset is an extension segment).
 *
 * Usage:
 * generate_table_keyed_config <initial font subset fil> <table keyed subset 1
 * file> [... <table keyed subset file n>]
 *
 * Where a subset file lists one codepoint per line in hexadecimal format:
 * 0xXXXX.
 */

int main(int argc, char** argv) {
  auto args = absl::ParseCommandLine(argc, argv);

  std::vector<btree_set<uint32_t>> sets;
  bool first = true;
  for (const char* arg : args) {
    if (first) {
      first = false;
      continue;
    }
    btree_set<uint32_t> set;
    auto result = util::LoadCodepointsOrdered(arg);
    if (!result.ok()) {
      std::cerr << "Failed to load codepoints from " << arg << ": " << result.status() << std::endl;
      return -1;
    }

    set.insert(result->begin(), result->end());
    sets.push_back(set);
  }

  EncoderConfig config;

  for (const auto& set : sets) {
    *config.add_non_glyph_codepoint_segmentation() = ToSetProto<Codepoints>(set);
  }

  std::string config_string;
  TextFormat::PrintToString(config, &config_string);
  std::cout << config_string;

  return 0;
}