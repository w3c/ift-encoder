#include <algorithm>
#include <codecvt>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <locale>
#include <ostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "common/int_set.h"
#include "ift/freq/unicode_frequencies.h"
#include "util/load_codepoints.h"

using absl::StatusOr;
using common::CodepointSet;
using ift::freq::UnicodeFrequencies;

/*
 * Converts Riegeli unicode frequency data file into a human readable
 * representation (single codepoint probabilities only).
 */

ABSL_FLAG(bool, add_character, false,
          "Include the unicode character as a comment in the output.");

static std::string CodepointToUtf8(uint32_t cp) {
  std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> converter;
  return converter.to_bytes(static_cast<char32_t>(cp));
}

struct CodepointProbability {
  uint32_t codepoint;
  double probability;
};

int main(int argc, char** argv) {
  auto args = absl::ParseCommandLine(argc, argv);

  if (args.size() != 2) {
    std::cerr << "Usage:" << std::endl
              << "freq_data_to_sorted_codepoints <riegeli_file>" << std::endl
              << std::endl
              << "Append @* to the file name to load sharded data files. "
              << "For example \"<path>@*\" will load all files of the form <path>-?????-of-?????"
              << std::endl;
    return -1;
  }

  const char* riegeli_file = args[1];
  StatusOr<UnicodeFrequencies> frequencies_status =
      util::LoadFrequenciesFromRiegeli(riegeli_file);

  if (!frequencies_status.ok()) {
    std::cerr << "Failed to load frequencies from " << riegeli_file << ": "
              << frequencies_status.status() << std::endl;
    return -1;
  }

  const UnicodeFrequencies& frequencies = *frequencies_status;
  CodepointSet codepoints = frequencies.CoveredCodepoints();

  std::vector<CodepointProbability> codepoint_probabilities;
  for (uint32_t cp : codepoints) {
    codepoint_probabilities.push_back({cp, frequencies.ProbabilityFor(cp)});
  }

  std::sort(codepoint_probabilities.begin(), codepoint_probabilities.end(),
            [](const CodepointProbability& a, const CodepointProbability& b) {
              if (a.probability != b.probability) {
                return a.probability > b.probability;
              }
              return a.codepoint < b.codepoint;
            });

  std::cout << std::left << std::setw(10) << "codepoint" << std::setw(16)
            << "probability" << std::endl;
  for (const auto& cp_prob : codepoint_probabilities) {
    std::stringstream ss;
    ss << "0x" << std::hex << cp_prob.codepoint;
    std::cout << std::left << std::setw(10) << ss.str() << std::fixed
              << std::setprecision(10) << std::setw(16) << cp_prob.probability;
    if (absl::GetFlag(FLAGS_add_character)) {
      std::cout << " # " << CodepointToUtf8(cp_prob.codepoint);
    }
    std::cout << std::endl;
  }

  return 0;
}
