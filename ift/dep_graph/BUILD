load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")


cc_library(
    name = "dep_graph",
    srcs = select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_graph.h",
            "dependency_graph.cc",
            "node.h",
            "traversal.h",
        ],
        "//conditions:default": [],
    }),
    visibility = [
        "//ift/encoder:__pkg__",
    ],
    deps = [
        "//ift/encoder:common",
        "//ift/encoder:segmentation_info",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
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
        "//common:testdata",
    ],
    deps = [
        ":dep_graph",
        "@googletest//:gtest_main",
        "//ift/encoder:segmentation_info",
    ],
)