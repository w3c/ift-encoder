load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

cc_library(
    name = "common",
    srcs = [
        "axis_range.cc",
        "bit_input_buffer.cc",
        "bit_output_buffer.cc",
        "brotli_binary_diff.cc",
        "brotli_binary_patch.cc",
        "compat_id.cc",
        "file_font_provider.cc",
        "font_data.cc",
        "font_helper.cc",
        "hb_set_unique_ptr.cc",
        "sparse_bit_set.cc",
        "woff2.cc",
    ],
    hdrs = [
        "axis_range.h",
        "binary_diff.h",
        "binary_patch.h",
        "bit_input_buffer.h",
        "bit_output_buffer.h",
        "branch_factor.h",
        "brotli_binary_diff.h",
        "brotli_binary_patch.h",
        "compat_id.h",
        "file_font_provider.h",
        "font_data.h",
        "font_helper.h",
        "font_helper_macros.h",
        "font_provider.h",
        "hasher.h",
        "hb_set_unique_ptr.h",
        "indexed_data_reader.h",
        "int_set.h",
        "sparse_bit_set.h",
        "woff2.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":try",
        "//brotli:shared_brotli_encoder",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@brotli//:brotli_inc",
        "@brotli//:brotlidec",
        "@brotli//:brotlienc",
        "@harfbuzz",
        "@woff2",
    ],
)

cc_library(
    name = "try",
    hdrs = ["try.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "data_file_resolver",
    srcs = [
        "bazel_data_file_resolver.cc",
    ],
    hdrs = [
        "bazel_data_file_resolver.h",
        "data_file_resolver.h",
        "fixed_data_file_resolver.h",
    ],
    data = [
        "@derived_normalization_props//file",
        "@ift_encoder_data//:data/metadata.binpb",
        "@ift_encoder_data//:freq_data",
        "@unicode_data//file",
    ],
    defines = [
        "FREQ_DATA_METADATA=\\\"$(rlocationpath @ift_encoder_data//:data/metadata.binpb)\\\"",
        "UNICODE_DATA_PATH=\\\"$(rlocationpath @unicode_data//file)\\\"",
        "DERIVED_PROPS_PATH=\\\"$(rlocationpath @derived_normalization_props//file)\\\"",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)

cc_library(
    name = "test_font_loader",
    testonly = True,
    srcs = ["bazel_test_font_loader.cc"],
    hdrs = [
        "bazel_test_font_loader.h",
        "test_font_loader.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":common",
        ":try",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
        "@harfbuzz",
    ],
)

cc_library(
    name = "mocks",
    srcs = [
        "mock_binary_diff.h",
        "mock_binary_patch.h",
        "mock_font_provider.h",
    ],
    hdrs = [
        "mock_binary_diff.h",
        "mock_binary_patch.h",
        "mock_font_provider.h",
    ],
    visibility = [
    ],
    deps = [
        ":common",
        "@googletest//:gtest_main",
    ],
)

filegroup(
    name = "testdata",
    srcs = glob(["testdata/**"]),
    visibility = [
        "//brotli:__pkg__",
        "//ift:__subpackages__",
        "//util:__pkg__",
    ],
)

cc_test(
    name = "common_test",
    size = "small",
    srcs = [
        "axis_range_test.cc",
        "bit_buffer_test.cc",
        "bit_input_buffer_test.cc",
        "bit_output_buffer_test.cc",
        "branch_factor_test.cc",
        "brotli_patching_test.cc",
        "file_font_provider_test.cc",
        "font_helper_test.cc",
        "indexed_data_reader_test.cc",
        "int_set_test.cc",
        "sparse_bit_set_test.cc",
        "woff2_test.cc",
    ],
    data = [
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":common",
        ":mocks",
        ":test_font_loader",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/hash:hash_test",
        "@abseil-cpp//absl/hash:hash_testing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@harfbuzz",
    ],
)
