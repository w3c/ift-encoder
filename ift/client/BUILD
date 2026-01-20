load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "fontations",
    srcs = [
        "fontations_client.cc",
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
        "//common",
        "//ift/encoder",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/status",
    ],
)
