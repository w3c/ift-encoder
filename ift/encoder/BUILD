cc_library(
    name = "encoder",
    srcs = [
        "closure_glyph_segmenter.cc",
        "closure_glyph_segmenter.h",
        "compiler.cc",
        "compiler.h",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    visibility = [
        "//ift:__subpackages__",
        "//ift/client:__pkg__",
        "//js_client:__pkg__",
        "//util:__pkg__",
    ],
    deps = [
        ":activation_condition",
        "//ift",
        "//ift/proto",
        "//ift/feature_registry:feature_registry",
        "//util:segmentation_plan_cc_proto",
        "//util:common_cc_proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:initialize",
        "@harfbuzz",
        ":common",
        ":segmentation_context",
    ],
)

cc_library(
    name = "glyph_partition",
    srcs = [
        "glyph_partition.h",
        "glyph_partition.cc",
    ],
    deps = [
        ":common",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "segmentation_context",
    srcs = [
        "segmentation_context.h",
        "segmentation_context.cc",
        "glyph_closure_cache.h",
        "glyph_closure_cache.cc",
        "requested_segmentation_information.h",
        "requested_segmentation_information.cc",
        "glyph_condition_set.h",
        "glyph_groupings.h",
        "glyph_groupings.cc",
        "complex_condition_finder.h",
        "complex_condition_finder.cc",
        "candidate_merge.cc",
        "candidate_merge.h",
        "patch_size_cache.h",
        "estimated_patch_size_cache.h",
        "estimated_patch_size_cache.cc",
        "merge_strategy.h",
        "merge_strategy.cc",
        "merger.cc",
        "merger.h",
        "invalidation_set.h",

    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    visibility = [
    ],
    deps = [
        ":glyph_partition",
        ":common",
        ":activation_condition",
        "//ift/feature_registry",
        "//ift",
        "//ift/freq",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:node_hash_set",
    ],
)

cc_library(
    name = "activation_condition",
    srcs = [
        "activation_condition.h",
        "activation_condition.cc",
        "glyph_segmentation.cc",
        "glyph_segmentation.h",
    ],
    deps = [
        "//ift/freq",
        ":common",
    ],
)

cc_library(
    name = "common",
    srcs = [
        "subset_definition.cc",
        "subset_definition.h",
        "types.h",
        "init_subset_defaults.h",
        "segment.h",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "//common",
        "//ift/proto",
        "//ift/freq:common",
        "//util:segmentation_plan_cc_proto",
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
    data = [
        "//common:testdata",
    ],
    deps = [
        ":encoder",
        "@harfbuzz",
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
        ":activation_condition",
        "//common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "complex_condition_finder_test",
    size = "medium",
    srcs = [
        "complex_condition_finder_test.cc",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":segmentation_context",
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
    name = "glyph_groupings_test",
    srcs = ["glyph_groupings_test.cc"],
    data = [
        "//common:testdata",
    ],
    deps = [
        ":segmentation_context",
        "//common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "candidate_merge_test",
    size = "small",
    srcs = [
        "candidate_merge_test.cc",
        "mock_patch_size_cache.h",
    ],
    data = [
        "//common:testdata",
    ],
    deps = [
        ":encoder",
        "//common",
        "//ift/freq:mock_probability_calculator",
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
        ":activation_condition",
        "//ift/freq:mock_probability_calculator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "glyph_partition_test",
    srcs = ["glyph_partition_test.cc"],
    deps = [
        ":glyph_partition",
        "//common",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "estimated_patch_size_cache_test",
    srcs = ["estimated_patch_size_cache_test.cc"],
    data = [
        "//common:testdata",
    ],
    deps = [
        ":segmentation_context",
        "//common",
        "@googletest//:gtest_main",
    ],
)