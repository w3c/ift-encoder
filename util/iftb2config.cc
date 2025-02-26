/*
 * This utility converts an iftb info dump into the corresponding
 * encoding_config.proto config file.
 *
 * Takes the info dump on stdin and outputs the config on stdout.
 */

#include <google/protobuf/text_format.h>

#include <iostream>
#include <sstream>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/font_data.h"
#include "common/try.h"
#include "util/convert_iftb.h"

using absl::StatusOr;
using absl::StrCat;
using common::FontData;
using common::hb_blob_unique_ptr;
using common::hb_face_unique_ptr;
using common::make_hb_blob;

ABSL_FLAG(std::string, font, "font.ttf",
          "The font file that corresponds to the IFTB dump.");

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

int main(int argc, char** argv) {
  auto args = absl::ParseCommandLine(argc, argv);

  auto face = load_font(absl::GetFlag(FLAGS_font).c_str());
  if (!face.ok()) {
    std::cerr << "Failed to load font " << absl::GetFlag(FLAGS_font) << " " << face.status() << std::endl;
  }

  std::stringstream ss;
  ss << std::cin.rdbuf();
  std::string input = ss.str();

  auto config = util::convert_iftb(input, face->get());
  if (!config.ok()) {
    std::cerr << "Failure parsing iftb info dump: " << config.status()
              << std::endl;
    return -1;
  }

  std::string out;
  google::protobuf::TextFormat::PrintToString(*config, &out);

  std::cout << out << std::endl;
  return 0;
}