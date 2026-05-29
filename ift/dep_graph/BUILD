load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "unicode_edges",
    srcs = ["unicode_edges.cc"],
    hdrs = ["unicode_edges.h"],
    deps = [
        "//ift/encoder:common",
        "//ift/common:data_file_resolver",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
        "@harfbuzz",
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
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "@googletest//:gtest_main",
        "//ift/encoder:segmentation_info",
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
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "@googletest//:gtest_main",
    ],
)