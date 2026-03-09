load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "encoder",
    srcs = [
        "closure_glyph_segmenter.cc",
        "compiler.cc",
    ],
    hdrs = [
      "closure_glyph_segmenter.h",
      "compiler.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":merge_strategy",
        ":activation_condition",
        ":common",
        ":segmentation_context",
        "//ift",
        "//ift/feature_registry",
        "//ift/proto",
        "//util:common_cc_proto",
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

cc_library(
    name = "glyph_partition",
    srcs = [
        "glyph_partition.cc",
        "glyph_partition.h",
    ],
    deps = [
        ":common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "segmentation_context",
    srcs = [
        "candidate_merge.cc",
        "candidate_merge.h",
        "complex_condition_finder.cc",
        "complex_condition_finder.h",
        "dependency_closure.h",
        "estimated_patch_size_cache.cc",
        "estimated_patch_size_cache.h",
        "glyph_condition_set.h",
        "glyph_condition_set.cc",
        "glyph_groupings.cc",
        "glyph_groupings.h",
        "invalidation_set.h",
        "merger.cc",
        "merger.h",
        "patch_size_cache.h",
        "requested_segmentation_information.cc",
        "requested_segmentation_information.h",
        "segmentation_context.cc",
        "segmentation_context.h",
    ] + select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_closure.cc",
        ],
        "//conditions:default": [
            "dependency_closure_disabled.cc",
        ],
    }),
    visibility = [
    ],
    deps = [
        ":merge_strategy",
        ":segmentation_info",
        ":activation_condition",
        ":common",
        ":glyph_partition",
        "//ift",
        "//ift/dep_graph",
        "//ift/feature_registry",
        "//ift/freq",
        "//util:segmenter_config_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "segmentation_info",
    srcs = [
        "requested_segmentation_information.cc",
        "requested_segmentation_information.h",
        "glyph_closure_cache.cc",
        "glyph_closure_cache.h",
    ],
    visibility = [
        "//ift/dep_graph:__pkg__",
    ],
    deps = [
        ":common",
        "//ift/feature_registry",
    ],
)

cc_library(
    name = "activation_condition",
    srcs = [
        "activation_condition.cc",
        "glyph_segmentation.cc",
    ],
    hdrs = [
        "activation_condition.h",
        "glyph_segmentation.h",
    ],
    deps = [
        ":common",
        "//ift/freq",
    ],
    visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "common",
    srcs = [
        "subset_definition.cc",
    ],
    hdrs = [
        "init_subset_defaults.h",
        "segment.h",
        "subset_definition.h",
        "types.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//common",
        "//ift/freq:common",
        "//ift/proto",
        "//util:segmentation_plan_cc_proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_library(
    name = "merge_strategy",
    srcs = [
        "merge_strategy.cc",
    ],
    hdrs = [
        "merge_strategy.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//ift/freq",
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
        "@googletest//:gtest_main",
        "@harfbuzz",
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
        ":activation_condition",
        ":encoder",
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
        ":encoder",
        ":segmentation_context",
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
    name = "glyph_closure_cache_test",
    size = "small",
    srcs = [
        "glyph_closure_cache_test.cc",
    ],
    data = [
        "//common:testdata",
    ],
    deps = [
        ":encoder",
        ":segmentation_info",
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
        ":activation_condition",
        ":encoder",
        "//ift/freq:mock_probability_calculator",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "dependency_closure_test",
    size = "small",
    srcs = select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_closure_test.cc",
        ],
        "//conditions:default": [],
    }),
    data = [
        "//common:testdata",
    ],
    deps = [
        ":segmentation_context",
        "//util:common_cc_proto",
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
