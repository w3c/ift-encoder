#include "ift/client/fontations_client.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "tools/cpp/runfiles/runfiles.h"
#include "ift/common/axis_range.h"
#include "ift/common/font_data.h"
#include "ift/common/int_set.h"
#include "ift/encoder/compiler.h"
#include "ift/common/try.h"

using absl::btree_set;
using absl::flat_hash_map;
using absl::Status;
using absl::StatusOr;
using ift::common::AxisRange;
using ift::common::FontData;
using ift::common::IntSet;
using ift::common::make_hb_blob;
using ift::common::make_hb_face;
using ift::encoder::Compiler;
using bazel::tools::cpp::runfiles::Runfiles;

namespace ift::client {

StatusOr<std::string> ResolvePath(const std::string& runfile_path) {
  std::string error;
  std::unique_ptr<Runfiles> runfiles(std::move(Runfiles::CreateForTest(&error)));
  if (!runfiles) {
    return absl::InternalError("fontations_client.cc: Failed to init runfiles.");
  }
  return runfiles->Rlocation(runfile_path);
}

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

StatusOr<std::string> Exec(const std::vector<std::string>& argv) {
  if (argv.empty()) {
    return absl::InvalidArgumentError("Empty argv");
  }

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    return absl::InternalError("pipe failed");
  }

  pid_t pid = fork();
  if (pid == -1) {
    close(pipefd[0]);
    close(pipefd[1]);
    return absl::InternalError("fork failed");
  }

  if (pid == 0) {
    // Child
    close(pipefd[0]); // Close read end
    if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
      _exit(127);
    }
    close(pipefd[1]);

    std::vector<char*> c_argv;
    for (const auto& arg : argv) {
      c_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    c_argv.push_back(nullptr);

    execvp(c_argv[0], c_argv.data());
    _exit(127); // exec failed
  }

  // Parent
  close(pipefd[1]); // Close write end

  std::string result;
  std::array<char, 128> buffer;
  ssize_t bytes_read;
  while ((bytes_read = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
    result.append(buffer.data(), bytes_read);
  }
  close(pipefd[0]);

  int status;
  if (waitpid(pid, &status, 0) == -1) {
    return absl::InternalError("waitpid failed");
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return absl::InternalError("command failed");
  }

  return result;
}

Status ToGraph(const Compiler::Encoding& encoding, graph& out,
               bool include_patch_paths) {
  auto font_path = WriteFontToDisk(encoding);
  if (!font_path.ok()) {
    return font_path.status();
  }

  std::vector<std::string> argv = {
      TRY(ResolvePath(IFT_GRAPH_PATH)),
      absl::StrCat("--font=", *font_path)};
  if (include_patch_paths) {
    argv.push_back("--include-patch-paths");
  }

  auto r = Exec(argv);
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
  std::vector<std::string> argv = {
      TRY(ResolvePath(IFT_EXTEND_PATH)),
      absl::StrCat("--font=", font_path.string()),
      absl::StrCat("--unicodes=", unicodes),
      absl::StrCat("--design-space=", design_space_str),
      absl::StrCat("--features=", features),
      absl::StrCat("--max-round-trips=", max_round_trips),
      absl::StrCat("--max-fetches=", max_fetches),
      absl::StrCat("--output=", output.string())};
  auto r = Exec(argv);
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
  absl::flat_hash_map<hb_tag_t, ift::common::AxisRange> design_space;
  return ExtendWithDesignSpace(encoding, codepoints, {}, design_space, nullptr,
                               max_round_trips, max_fetches);
}

}  // namespace ift::client