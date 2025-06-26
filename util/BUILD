proto_library(
    name = "segmentation_plan_proto",
    srcs = ["segmentation_plan.proto"],
)

cc_proto_library(
    name = "segmentation_plan_cc_proto",
    deps = [":segmentation_plan_proto"],
    visibility = [
        "//ift/encoder:__pkg__",
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
        "//common",
        ":segmentation_plan_cc_proto",
        ":load_codepoints",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
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
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
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
    ],
    deps = [
        ":load_codepoints",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "iftb2config",
    srcs = [
        "iftb2config.cc",
    ],
    deps = [
        ":convert_iftb",
        ":segmentation_plan_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
