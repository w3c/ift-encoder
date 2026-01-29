load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "dep_graph",
    srcs = [
        "dependency_graph.h",
        "dependency_graph.cc",
        "node.h",
        "traversal.h",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
        "-DHB_DEPEND_API",
    ],
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
    srcs = [
        "dependency_graph_test.cc",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
        "-DHB_DEPEND_API",
    ],
    data = [
        "//common:testdata",
    ],
    deps = [
        ":dep_graph",
        "@googletest//:gtest_main",
        "//ift/encoder:segmentation_info",
    ],
)