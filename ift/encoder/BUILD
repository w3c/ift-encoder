cc_library(
    name = "encoder",
    srcs = [
        "activation_condition.h",
        "activation_condition.cc",
        "types.h",
        "closure_glyph_segmenter.cc",
        "closure_glyph_segmenter.h",
        "compiler.cc",
        "compiler.h",
        "glyph_segmentation.cc",
        "glyph_segmentation.h",
        "subset_definition.cc",
        "subset_definition.h",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    visibility = [
        "//ift:__pkg__",
        "//ift/client:__pkg__",
        "//js_client:__pkg__",
        "//util:__pkg__",
    ],
    deps = [
        "//ift",
        "//ift/proto",
        "//util:segmentation_plan_cc_proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:initialize",
        "@harfbuzz",
    ],
)

cc_test(
    name = "compiler_test",
    size = "small",
    srcs = [
        "compiler_test.cc",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":encoder",
        "//common",
        "//ift:test_segments",
        "//ift/client:fontations",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "subset_definition_test",
    size = "small",
    srcs = [
        "subset_definition_test.cc",
    ],
    deps = [
        ":encoder",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "glyph_segmentation_test",
    size = "small",
    srcs = [
        "glyph_segmentation_test.cc",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":encoder",
        "//common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "closure_glyph_segmenter_test",
    size = "medium",
    srcs = [
        "closure_glyph_segmenter_test.cc",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":encoder",
        "//common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "activation_condition_test",
    size = "small",
    srcs = [
        "activation_condition_test.cc",
    ],
    deps = [
        ":encoder",
        "@googletest//:gtest_main",
    ],
)
