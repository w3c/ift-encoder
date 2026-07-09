load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

cc_library(
    name = "unicode_edges",
    srcs = ["unicode_edges.cc"],
    hdrs = ["unicode_edges.h"],
    visibility = [
        "//ift/encoder:__pkg__",
    ],
    deps = [
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:try",
        "//ift/encoder:common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
        "@harfbuzz",
    ],
)

cc_library(
    name = "dep_graph",
    srcs = select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_graph.cc",
        ],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_graph.h",
            "node.h",
            "pending_edge.h",
            "traversal.h",
        ],
        "//conditions:default": [],
    }),
    visibility = [
        "//ift/encoder:__pkg__",
    ],
    deps = [
        ":unicode_edges",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/encoder:common",
        "//ift/encoder:segmentation_info",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
        "@harfbuzz",
    ],
)

cc_test(
    name = "dependency_graph_test",
    size = "small",
    srcs = select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_graph_test.cc",
        ],
        "//conditions:default": [],
    }),
    data = [
        "//ift/common:testdata",
    ],
    deps = [
        ":dep_graph",
        ":unicode_edges",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/encoder:common",
        "//ift/encoder:segmentation_info",
        "//ift/freq:common",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "unicode_edges_test",
    size = "small",
    srcs = ["unicode_edges_test.cc"],
    data = [
        "//ift/common:testdata",
    ],
    deps = [
        ":unicode_edges",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
