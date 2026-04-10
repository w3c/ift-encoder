load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

proto_library(
    name = "segmentation_plan_proto",
    srcs = ["segmentation_plan.proto"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":common_proto",
    ],
)

cc_proto_library(
    name = "segmentation_plan_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":segmentation_plan_proto",
    ],
)

proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
    visibility = [
        "//visibility:public",
    ],
)

cc_proto_library(
    name = "common_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":common_proto"],
)

proto_library(
    name = "segmenter_config_proto",
    srcs = ["segmenter_config.proto"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":common_proto",
        ":segmentation_plan_proto",
    ],
)

cc_proto_library(
    name = "segmenter_config_cc_proto",
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":segmenter_config_proto",
    ],
)

cc_library(
    name = "config_compiler",
    srcs = [
        "config_compiler.cc",
    ],
    hdrs = [
        "config_compiler.h",
    ],
    deps = [
        ":load_codepoints",
        ":segmentation_plan_cc_proto",
        "//ift/common",
        "//ift/encoder",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
    visibility = [
        "//visibility:public",
    ]
)

cc_library(
    name = "auto_segmenter_config",
    srcs = [
        "auto_segmenter_config.cc",
    ],
    hdrs = [
        "auto_segmenter_config.h",
    ],
    deps = [
        ":load_codepoints",
        ":segmenter_config_cc_proto",
        "//ift/common",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@harfbuzz",
    ],
    visibility = [
        "//visibility:public",
    ]
)

cc_library(
    name = "load_codepoints",
    srcs = [
        "load_codepoints.cc",
    ],
    hdrs = [
        "load_codepoints.h",
    ],
    deps = [
        "//ift/common",
        "//ift/freq",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
        "@ift_encoder_data//:codepoint_count_cc_proto",
        "@ift_encoder_data//:metadata_cc_proto",
        "@riegeli//riegeli/bytes:fd_reader",
        "@riegeli//riegeli/records:record_reader",
    ],
    visibility = [
        "//visibility:public",
    ]
)

cc_library(
    name = "segmenter_config_util",
    srcs = [
        "segmenter_config_util.cc",
    ],
    hdrs = [
        "segmenter_config_util.h",
    ],
    deps = [
        ":load_codepoints",
        ":segmentation_plan_cc_proto",
        ":segmenter_config_cc_proto",
        "//ift/common",
        "//ift/encoder",
        "@abseil-cpp//absl/status:statusor",
        "@harfbuzz",
    ],
    visibility = [
        "//visibility:public",
    ]
)

cc_test(
    name = "auto_segmenter_config_test",
    size = "small",
    srcs = [
        "auto_segmenter_config_test.cc",
    ],
    data = [
        "//ift/common:testdata",
        "@ift_encoder_data//:freq_data",
    ],
    deps = [
        ":auto_segmenter_config",
        "//ift/common",
        "@googletest//:gtest_main",
        "@harfbuzz",
    ],
)

cc_test(
    name = "load_codepoints_test",
    size = "small",
    srcs = [
        "load_codepoints_test.cc",
    ],
    data = [
        "//util:testdata",
        "@ift_encoder_data//:freq_data",
    ],
    deps = [
        ":load_codepoints",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "segmenter_config_util_test",
    size = "small",
    srcs = [
        "segmenter_config_util_test.cc",
    ],
    data = [
        "//util:testdata",
    ],
    deps = [
        ":segmenter_config_util",
        "@googletest//:gtest_main",
    ],
)
