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