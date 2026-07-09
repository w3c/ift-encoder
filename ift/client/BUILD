load("@rules_cc//cc:cc_library.bzl", "cc_library")

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
    visibility = [
        "//ift:__subpackages__",
    ],
    deps = [
        "//ift/common",
        "//ift/encoder",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
    ],
)
