load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_binary(
    name = "font2ift",
    srcs = [
        "font2ift.cc",
    ],
    data = [
        "@ift_encoder_data//:freq_data",
    ],
    deps = [
        ":auto_config_flags",
        "//ift/config:auto_segmenter_config",
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/config:segmenter_config_util",
        "//ift/common",
        "//ift",
        "//ift/encoder",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
        "//ift/config:segmenter_config_cc_proto",
    ],
)

cc_binary(
    name = "generate_table_keyed_config",
    srcs = [
        "generate_table_keyed_config.cc",
    ],
    deps = [
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_binary(
    name = "closure_glyph_keyed_segmenter_util",
    srcs = [
        "closure_glyph_keyed_segmenter_util.cc",
    ],
    data = [
        "@ift_encoder_data//:freq_data",
    ],
    deps = [
        ":auto_config_flags",
        "//ift/config:auto_segmenter_config",
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/config:segmenter_config_cc_proto",
        "//ift/config:segmenter_config_util",
        "//ift/common",
        "//ift",
        "//ift/encoder",
        "//ift/freq",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
)

cc_binary(
    name = "trace_segmentation_plan",
    srcs = [
        "trace_segmentation_plan.cc",
    ],
    deps = [
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/common",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "auto_config_flags",
    srcs = ["auto_config_flags.cc"],
    hdrs = ["auto_config_flags.h"],
    deps = [
        "@abseil-cpp//absl/flags:flag",
    ],
)

cc_binary(
    name = "generate_segmenter_config",
    srcs = [
        "generate_segmenter_config.cc",
    ],
    data = [
        "@ift_encoder_data//:freq_data",
    ],
    deps = [
        "//ift/config:auto_segmenter_config",
        "//ift/config:load_codepoints",
        "//ift/config:segmenter_config_cc_proto",
        "//ift/common",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/status",
        "@harfbuzz",
        "@protobuf",
    ],
)

cc_binary(
    name = "generate_riegeli_test_data",
    srcs = [
        "generate_riegeli_test_data.cc",
    ],
    deps = [
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status",
        "@riegeli//riegeli/bytes:fd_writer",
        "@riegeli//riegeli/records:record_writer",
        "@ift_encoder_data//:codepoint_count_cc_proto",
    ],
)

cc_binary(
    name = "freq_data_to_sorted_codepoints",
    srcs = [
        "freq_data_to_sorted_codepoints.cc",
    ],
    deps = [
        "//ift/config:load_codepoints",
        "//ift/freq",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
    ],
)

filegroup(
    name = "testdata",
    srcs = glob([
        "testdata/**",
    ]),
    visibility = ["//:__subpackages__"],
)
