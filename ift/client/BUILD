load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

cc_library(
    name = "fontations",
    srcs = [
        "fontations_client.cc",
    ],
    hdrs = [
        "fontations_client.h",
    ],
    data = [
        "@fontations//:ift_extend",
        "@fontations//:ift_graph",
    ],
    defines = [
        "IFT_EXTEND_PATH=\\\"$(rlocationpath @fontations//:ift_extend)\\\"",
        "IFT_GRAPH_PATH=\\\"$(rlocationpath @fontations//:ift_graph)\\\"",
    ],
    visibility = [
        "//ift:__subpackages__",
    ],
    deps = [
        "//ift/common",
        "//ift/common:try",
        "//ift/encoder",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)

