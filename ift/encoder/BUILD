load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

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
        ":activation_condition",
        ":common",
        ":merge_strategy",
        ":segmentation_context",
        ":segmentation_info",
        "//ift",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:try",
        "//ift/config:common_cc_proto",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/config:segmenter_config_cc_proto",
        "//ift/feature_registry",
        "//ift/freq",
        "//ift/freq:common",
        "//ift/proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
)

cc_library(
    name = "glyph_partition",
    srcs = [
        "glyph_partition.cc",
    ],
    hdrs = [
        "glyph_partition.h",
    ],
    deps = [
        ":common",
        "//ift/common",
        "//ift/common:try",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "segmentation_context",
    srcs = [
        "candidate_merge.cc",
        "complex_condition_finder.cc",
        "estimated_patch_size_cache.cc",
        "glyph_condition_set.cc",
        "glyph_groupings.cc",
        "merger.cc",
        "segmentation_context.cc",
    ] + select({
        "//:use_harfbuzz_dep_graph": [
            "dependency_closure.cc",
        ],
        "//conditions:default": [
            "dependency_closure_disabled.cc",
        ],
    }),
    hdrs = [
        "candidate_merge.h",
        "complex_condition_finder.h",
        "condition_to_glyphs_index.h",
        "dependency_closure.h",
        "estimated_patch_size_cache.h",
        "glyph_condition_set.h",
        "glyph_groupings.h",
        "invalidation_set.h",
        "merger.h",
        "patch_size_cache.h",
        "segmentation_context.h",
    ],
    deps = [
        ":activation_condition",
        ":common",
        ":glyph_partition",
        ":merge_strategy",
        ":segmentation_info",
        "//ift",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:try",
        "//ift/config:segmenter_config_cc_proto",
        "//ift/dep_graph",
        "//ift/feature_registry",
        "//ift/freq",
        "//ift/freq:common",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
)

cc_library(
    name = "segmentation_info",
    srcs = [
        "glyph_closure_cache.cc",
        "requested_segmentation_information.cc",
    ],
    hdrs = [
        "glyph_closure_cache.h",
        "init_subset_defaults.h",
        "requested_segmentation_information.h",
    ],
    visibility = [
        "//ift/dep_graph:__pkg__",
    ],
    deps = [
        ":common",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:try",
        "//ift/config:common_cc_proto",
        "//ift/dep_graph:unicode_edges",
        "//ift/feature_registry",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "activation_condition",
    srcs = [
        "activation_condition.cc",
        "entry_graph.cc",
        "glyph_segmentation.cc",
    ],
    hdrs = [
        "activation_condition.h",
        "entry_graph.h",
        "glyph_segmentation.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":common",
        "//ift/common",
        "//ift/common:try",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/freq",
        "//ift/freq:common",
        "//ift/proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "common",
    srcs = [
        "subset_definition.cc",
    ],
    hdrs = [
        "segment.h",
        "subset_definition.h",
        "types.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//ift/common",
        "//ift/common:try",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/freq:common",
        "//ift/proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@harfbuzz",
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
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":encoder",
        "//ift:test_segments",
        "//ift/client:fontations",
        "//ift/common",
        "//ift/common:test_font_loader",
        "//ift/proto",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "requested_segmentation_information_test",
    size = "small",
    srcs = [
        "requested_segmentation_information_test.cc",
    ],
    data = [
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":encoder",
        ":segmentation_info",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/freq:common",
        "@googletest//:gtest",
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
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":encoder",
        "//ift/common",
        "//ift/common:test_font_loader",
        "//ift/proto",
        "@googletest//:gtest",
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
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":activation_condition",
        ":common",
        ":encoder",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/freq",
        "//ift/proto",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

cc_test(
    name = "complex_condition_finder_test",
    size = "medium",
    srcs = [
        "complex_condition_finder_test.cc",
    ],
    data = [
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":encoder",
        ":segmentation_context",
        ":segmentation_info",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/freq:common",
        "@abseil-cpp//absl/container:btree",
        "@googletest//:gtest",
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
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":activation_condition",
        ":common",
        ":encoder",
        ":merge_strategy",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/common:try",
        "//ift/freq",
        "//ift/freq:mock_probability_calculator",
        "@googletest//:gtest",
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
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":encoder",
        ":segmentation_info",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/freq:common",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "glyph_groupings_test",
    srcs = ["glyph_groupings_test.cc"],
    data = [
        "//ift/common:testdata",
    ],
    deps = [
        ":activation_condition",
        ":common",
        ":segmentation_context",
        ":segmentation_info",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/freq:common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@googletest//:gtest",
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
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":encoder",
        ":merge_strategy",
        ":segmentation_context",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/freq",
        "//ift/freq:common",
        "//ift/freq:mock_probability_calculator",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest",
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
        ":common",
        ":encoder",
        "//ift/common",
        "//ift/freq:common",
        "//ift/freq:mock_probability_calculator",
        "//ift/proto",
        "@googletest//:gtest",
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
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":segmentation_context",
        ":segmentation_info",
        "//ift/common",
        "//ift/common:data_file_resolver",
        "//ift/common:test_font_loader",
        "//ift/config:common_cc_proto",
        "//ift/dep_graph",
        "//ift/freq:common",
        "@bazel_tools//tools/cpp/runfiles",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "glyph_partition_test",
    srcs = ["glyph_partition_test.cc"],
    deps = [
        ":glyph_partition",
        "//ift/common",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "estimated_patch_size_cache_test",
    srcs = ["estimated_patch_size_cache_test.cc"],
    data = [
        "//ift/common:testdata",
    ],
    deps = [
        ":segmentation_context",
        "//ift/common",
        "//ift/common:test_font_loader",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "entry_graph_test",
    size = "small",
    srcs = [
        "entry_graph_test.cc",
    ],
    deps = [
        ":activation_condition",
        ":common",
        "//ift/common",
        "//ift/proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "entry_graph_equivalence_test",
    size = "small",
    srcs = [
        "entry_graph_equivalence_test.cc",
    ],
    data = [
        "//ift/common:testdata",
    ],
    deps = [
        ":activation_condition",
        ":common",
        "//ift/common",
        "//ift/config:segmentation_plan_cc_proto",
        "//ift/proto",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

cc_test(
    name = "glyph_condition_set_test",
    size = "small",
    srcs = [
        "glyph_condition_set_test.cc",
    ],
    deps = [
        ":activation_condition",
        ":segmentation_context",
        "//ift/common",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
