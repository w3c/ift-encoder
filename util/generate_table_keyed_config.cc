#include <google/protobuf/text_format.h>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"

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
  // TODO
  return 0;
}