load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(features = ["layering_check"])

cc_binary(
    name = "font2ift",
    srcs = [
        "font2ift.cc",
    ],
    deps = [
        ":auto_config_flags",
        "//ift",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:try",
        "//ift/config:auto_segmenter_config",
        "//ift/config:config_compiler",
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/config:segmenter_config_cc_proto",
        "//ift/config:segmenter_config_util",
        "//ift/encoder",
        "//ift/encoder:activation_condition",
        "//ift/encoder:common",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
        "@protobuf",
    ],
)

cc_binary(
    name = "gen_ift_table_keyed_plan",
    srcs = [
        "gen_ift_table_keyed_plan.cc",
    ],
    deps = [
        "//ift/common",
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@protobuf",
    ],
)

cc_binary(
    name = "gen_ift_segmentation_plan",
    srcs = [
        "gen_ift_segmentation_plan.cc",
    ],
    deps = [
        ":auto_config_flags",
        "//ift",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:try",
        "//ift/config:auto_segmenter_config",
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/config:segmenter_config_cc_proto",
        "//ift/config:segmenter_config_util",
        "//ift/encoder",
        "//ift/encoder:activation_condition",
        "//ift/encoder:common",
        "//ift/encoder:merge_strategy",
        "//ift/freq",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
        "@protobuf",
    ],
)

cc_binary(
    name = "trace_segmentation_plan",
    srcs = [
        "trace_segmentation_plan.cc",
    ],
    deps = [
        "//ift/common",
        "//ift/common:try",
        "//ift/config:load_codepoints",
        "//ift/config:segmentation_plan_cc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@protobuf",
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
    name = "gen_ift_segmenter_config",
    srcs = [
        "gen_ift_segmenter_config.cc",
    ],
    deps = [
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:try",
        "//ift/config:auto_segmenter_config",
        "//ift/config:load_codepoints",
        "//ift/config:segmenter_config_cc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log:globals",
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
        "@abseil-cpp//absl/strings",
        "@ift_encoder_data//:codepoint_count_cc_proto",
        "@riegeli//riegeli/bytes:fd_writer",
        "@riegeli//riegeli/records:record_writer",
    ],
)

cc_binary(
    name = "freq_data_to_sorted_codepoints",
    srcs = [
        "freq_data_to_sorted_codepoints.cc",
    ],
    deps = [
        "//ift/common",
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
