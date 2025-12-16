// Small utility to generate test unicode frequency data files in the
// riegeli format.

#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "riegeli/bytes/fd_writer.h"
#include "riegeli/records/record_writer.h"
#include "util/unicode_count.pb.h"

using absl::StrCat;

ABSL_FLAG(std::string, output_path, "", "Path to write the output file.");

ABSL_FLAG(bool, include_invalid_record, false, "If set add an invalid record.");
ABSL_FLAG(bool, shard, false, "If set shard into multiple files.");

namespace {

absl::Status Main() {
  std::string output_path = absl::GetFlag(FLAGS_output_path);
  if (output_path.empty()) {
    return absl::InvalidArgumentError("output_path must be specified.");
  }

  CodepointCount message1;
  message1.add_codepoints(0x41);
  message1.add_codepoints(0x42);
  message1.set_count(100);

  CodepointCount message2;
  message2.add_codepoints(0x43);
  message2.set_count(200);

  CodepointCount message3;
  message3.add_codepoints(0x44);
  message3.add_codepoints(0x45);
  message3.set_count(50);

  CodepointCount message4;
  message4.add_codepoints(0x44);
  message4.add_codepoints(0x44);
  message4.set_count(75);

  if (!absl::GetFlag(FLAGS_shard)) {
    riegeli::RecordWriter writer{riegeli::FdWriter(output_path)};
    writer.WriteRecord(message1);
    writer.WriteRecord(message2);
    writer.WriteRecord(message3);
    writer.WriteRecord(message4);

    if (absl::GetFlag(FLAGS_include_invalid_record)) {
      CodepointCount message5;
      message5.add_codepoints(0x46);
      message5.add_codepoints(0x46);
      message5.add_codepoints(0x46);
      message5.set_count(75);
      writer.WriteRecord(message5);
    }

    if (!writer.Close()) {
      return writer.status();
    }
  } else {
    {
      riegeli::RecordWriter writer{
          riegeli::FdWriter(StrCat(output_path, "-00000-of-00003"))};
      writer.WriteRecord(message1);
      writer.WriteRecord(message2);
      writer.Close();
    }
    {
      riegeli::RecordWriter writer{
          riegeli::FdWriter(StrCat(output_path, "-00001-of-00003"))};
      writer.WriteRecord(message3);
      writer.Close();
    }
    {
      riegeli::RecordWriter writer{
          riegeli::FdWriter(StrCat(output_path, "-00002-of-00003"))};
      writer.WriteRecord(message4);
      writer.Close();
    }
  }

  return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::Status status = Main();
  if (!status.ok()) {
    std::cerr << status << std::endl;
    return -1;
  }
  return 0;
}
