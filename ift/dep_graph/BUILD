load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "unicode_edges",
    srcs = ["unicode_edges.cc"],
    hdrs = ["unicode_edges.h"],
    deps = [
        "//ift/encoder:common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
        "@harfbuzz",
    ],
    data = [
        "@unicode_data//file",
        "@derived_normalization_props//file",
    ],
    visibility = [
        "//ift/encoder:__pkg__",
    ],
)

cc_library(
    name = "dep_graph",
    srcs = select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_graph.h",
            "dependency_graph.cc",
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
        "//ift/encoder:common",
        "//ift/encoder:segmentation_info",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
        "@bazel_tools//tools/cpp/runfiles",
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
        "@googletest//:gtest_main",
        "//ift/encoder:segmentation_info",
    ],
)