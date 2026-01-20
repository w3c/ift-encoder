load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "ift",
    srcs = [
        "glyph_keyed_diff.cc",
        "glyph_keyed_diff.h",
        "table_keyed_diff.cc",
        "table_keyed_diff.h",
        "url_template.cc",
    ],
    hdrs = [
        "table_keyed_diff.h",
        "url_template.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//common",
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
        "//visibility:public",
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
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":ift",
        "//common",
        "//ift/encoder",
        "//ift/proto",
        "@abseil-cpp//absl/container:btree",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "integration_test",
    size = "medium",
    srcs = [
        "integration_test.cc",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":ift",
        ":test_segments",
        "//common",
        "//ift/client:fontations",
        "//ift/encoder",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@googletest//:gtest_main",
    ],
)

filegroup(
    name = "testdata",
    srcs = glob(["testdata/**"]),
    visibility = [
        "//common:__subpackages__",
        "//ift:__subpackages__",
    ],
)
