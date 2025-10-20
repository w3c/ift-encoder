proto_library(
    name = "segmentation_plan_proto",
    srcs = ["segmentation_plan.proto"],
    deps = [
        ":common_proto",
    ],
)

cc_proto_library(
    name = "segmentation_plan_cc_proto",
    deps = [
        ":segmentation_plan_proto",
    ],
    visibility = [
        "//ift/encoder:__pkg__",
    ]
)

proto_library(
    name = "unicode_count_proto",
    srcs = ["unicode_count.proto"],
)

cc_proto_library(
    name = "unicode_count_cc_proto",
    deps = [":unicode_count_proto"],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
    visibility = [
        "//visibility:public",
    ],
)

proto_library(
    name = "segmenter_config_proto",
    srcs = ["segmenter_config.proto"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":common_proto",
    ],
)

cc_proto_library(
    name = "segmenter_config_cc_proto",
    deps = [
        ":segmenter_config_proto",
    ],
    visibility = [
        "//ift/encoder:__pkg__",
        "//util:__pkg__",
    ]
)

cc_binary(
    name = "font2ift",
    srcs = [
        "font2ift.cc",
    ],
    deps = [
        "//ift",
        "//ift/encoder",
        "//common",
        ":load_codepoints",
        ":segmentation_plan_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
)

cc_binary(
    name = "generate_table_keyed_config",
    srcs = [
        "generate_table_keyed_config.cc",
    ],
    deps = [
        ":segmentation_plan_cc_proto",
        ":load_codepoints",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_binary(
    name = "closure_glyph_keyed_segmenter_util",
    srcs = [
        "closure_glyph_keyed_segmenter_util.cc",
    ],
    deps = [
        "//ift",
        "//ift/encoder",
        "//ift/freq",
        ":segmenter_config_cc_proto",
        "//common",
        ":segmentation_plan_cc_proto",
        ":segmenter_config_util",
        ":load_codepoints",
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
        ":segmentation_plan_cc_proto",
        ":load_codepoints",
        "//common",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/log:initialize",
    ],
)

cc_library(
    name = "convert_iftb",
    srcs = [
        "convert_iftb.cc",
        "convert_iftb.h",
    ],
    deps = [
        "//common",
        ":segmentation_plan_cc_proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
    ],
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
        "//common",
        "//ift/freq",
        ":unicode_count_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
        "@riegeli//riegeli/bytes:fd_reader",
        "@riegeli//riegeli/records:record_reader",
    ],
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
        "//common",
        "//ift/encoder",
        ":segmenter_config_cc_proto",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "convert_iftb_test",
    size = "small",
    srcs = [
        "convert_iftb_test.cc",
    ],
    data = [
        "testdata/convert-iftb-sample.txt",
        "testdata/Roboto-Regular.Awesome.ttf",
    ],
    deps = [
        ":convert_iftb",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "load_codepoints_test",
    size = "small",
    srcs = [
        "load_codepoints_test.cc",
    ],
    data = [
        "testdata/codepoints.txt",
        "testdata/codepoints_invalid_1.txt",
        "testdata/codepoints_invalid_2.txt",
        "testdata/codepoints_with_freq.txt",
         "testdata/codepoints_with_freq_invalid.txt",
         "testdata/test_freq_data.riegeli",
         "testdata/invalid_test_freq_data.riegeli",
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
        "testdata/test_freq_data.riegeli",
    ],
    deps = [
        ":segmenter_config_util",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "iftb2config",
    srcs = [
        "iftb2config.cc",
    ],
    deps = [
        ":load_codepoints",
        ":convert_iftb",
        ":segmentation_plan_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_binary(
    name = "generate_riegeli_test_data",
    srcs = [
        "generate_riegeli_test_data.cc",
    ],
    deps = [
        ":unicode_count_cc_proto",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status",
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
        ":load_codepoints",
        "//ift/freq",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
    ],
)
