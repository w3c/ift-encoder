load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

cc_library(
    name = "ift",
    srcs = [
        "glyph_keyed_diff.cc",
        "table_keyed_diff.cc",
        "url_template.cc",
    ],
    hdrs = [
        "glyph_keyed_diff.h",
        "table_keyed_diff.h",
        "url_template.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//ift/common",
        "//ift/common:try",
        "//ift/proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@cppcodec",
        "@harfbuzz",
    ],
)

cc_library(
    name = "test_segments",
    srcs = [
        "testdata/test_segments.h",
    ],
    hdrs = [
        "testdata/test_segments.h",
    ],
    visibility = [
        "//ift/encoder:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "ift_test",
    size = "small",
    srcs = [
        "glyph_keyed_diff_test.cc",
        "table_keyed_diff_test.cc",
        "url_template_test.cc",
    ],
    data = [
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":ift",
        "//ift/common",
        "//ift/common:test_font_loader",
        "//ift/encoder",
        "//ift/proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@harfbuzz",
    ],
)

cc_test(
    name = "integration_test",
    size = "medium",
    srcs = [
        "integration_test.cc",
    ],
    data = [
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":ift",
        ":test_segments",
        "//ift/client:fontations",
        "//ift/common",
        "//ift/common:test_font_loader",
        "//ift/common:try",
        "//ift/encoder",
        "//ift/encoder:common",
        "//ift/proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@harfbuzz",
    ],
)

filegroup(
    name = "testdata",
    srcs = glob(["testdata/**"]),
    visibility = [
        "//ift:__subpackages__",
        "//ift/common:__subpackages__",
    ],
)
