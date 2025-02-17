proto_library(
    name = "encoder_config_proto",
    srcs = ["encoder_config.proto"],
)

cc_proto_library(
    name = "encoder_config_cc_proto",
    deps = [":encoder_config_proto"],
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
        ":encoder_config_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
)

cc_binary(
    name = "glyph_keyed_segmenter",
    srcs = [
        "glyph_keyed_segmenter.cc",
    ],
    deps = [
        "//ift",
        "//ift/encoder",
        "//common",
        ":encoder_config_cc_proto",
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
        ":encoder_config_cc_proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
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
    ],
    deps = [
        ":convert_iftb",
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
        ":encoder_config_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
