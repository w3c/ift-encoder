#include "util/path_util.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace ift::util {

std::filesystem::path GetTestTempDir() {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  if (test_tmpdir == nullptr || test_tmpdir[0] == '\0') {
    std::cerr << "Failed to get temp directory for test." << std::endl;
    assert(false);
  }
  return std::filesystem::path(test_tmpdir);
}

TEST(PathUtilTest, ValidSubpaths) {
  auto temp_dir = GetTestTempDir() / "ValidSubpaths";
  std::filesystem::create_directories(temp_dir);

  auto path = JoinAndValidatePath(temp_dir.string(), "file.txt");
  EXPECT_TRUE(path.ok()) << path.status();
  EXPECT_EQ(*path, (temp_dir / "file.txt").string());

  path = JoinAndValidatePath(temp_dir.string(), "subdir/file.txt");
  EXPECT_TRUE(path.ok()) << path.status();
  EXPECT_EQ(*path, (temp_dir / "subdir" / "file.txt").string());

  path = JoinAndValidatePath(temp_dir.string(), "subdir/../file.txt");
  EXPECT_TRUE(path.ok()) << path.status();
  EXPECT_EQ(*path, (temp_dir / "file.txt").string());
}

TEST(PathUtilTest, DetectsPathTraversal) {
  auto temp_dir = GetTestTempDir() / "DetectsPathTraversal";
  std::filesystem::create_directories(temp_dir);

  auto path = JoinAndValidatePath(temp_dir.string(), "../outside.txt");
  EXPECT_FALSE(path.ok()) << path.status();
  EXPECT_TRUE(absl::IsPermissionDenied(path.status()))
      << "Expected permission denied, got: " << path.status();

  path = JoinAndValidatePath(temp_dir.string(), "../../../../tmp/outside.txt");
  EXPECT_FALSE(path.ok()) << path.status();
  EXPECT_TRUE(absl::IsPermissionDenied(path.status()))
      << "Expected permission denied, got: " << path.status();

  path = JoinAndValidatePath(temp_dir.string(), "/absolute/path");
  EXPECT_FALSE(path.ok()) << path.status();
  EXPECT_TRUE(absl::IsPermissionDenied(path.status()))
      << "Expected permission denied, got: " << path.status();

  auto base_dir = temp_dir / "foo";
  std::filesystem::create_directories(base_dir);
  path = JoinAndValidatePath(base_dir.string(), "../foo_bar/file.txt");
  EXPECT_FALSE(path.ok()) << path.status();
  EXPECT_TRUE(absl::IsPermissionDenied(path.status()))
      << "Expected permission denied, got: " << path.status();
}

TEST(PathUtilTest, EmptyBaseDir) {
  auto res = JoinAndValidatePath("", "file.txt");
  EXPECT_FALSE(res.ok());
  EXPECT_EQ(res.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace ift::util
