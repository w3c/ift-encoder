cc_library(
    name = "ift",
    srcs = [
        "glyph_keyed_diff.cc",
        "glyph_keyed_diff.h",
        "url_template.cc",
        "table_keyed_diff.cc",
        "table_keyed_diff.h",
    ],
    hdrs = [
        "url_template.h",
        "table_keyed_diff.h",
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
    deps = [
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
    visibility = [
        "//visibility:public",
    ],
)

cc_test(
    name = "ift_test",
    size = "small",
    srcs = [
        "glyph_keyed_diff_test.cc",
        "url_template_test.cc",
        "table_keyed_diff_test.cc",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    data = [
        "//ift:testdata",
        "//common:testdata",
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
        "//ift:testdata",
        "//common:testdata",
    ],
    deps = [
        ":ift",
        ":test_segments",
        "//common",
        "//ift/encoder",
        "//ift/client:fontations",
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
