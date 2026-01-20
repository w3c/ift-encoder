load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "common",
    srcs = [
        "axis_range.cc",
        "bit_input_buffer.cc",
        "bit_input_buffer.h",
        "bit_output_buffer.cc",
        "bit_output_buffer.h",
        "brotli_binary_diff.cc",
        "brotli_binary_patch.cc",
        "compat_id.cc",
        "compat_id.h",
        "file_font_provider.cc",
        "font_helper.cc",
        "hb_set_unique_ptr.cc",
        "indexed_data_reader.h",
        "sparse_bit_set.cc",
        "woff2.cc",
    ],
    hdrs = [
        "axis_range.h",
        "binary_diff.h",
        "binary_patch.h",
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
        "int_set.h",
        "sparse_bit_set.h",
        "try.h",
        "woff2.h",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//brotli:shared_brotli_encoder",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@brotli//:brotlidec",
        "@brotli//:brotlienc",
        "@harfbuzz",
        "@woff2",
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
        "//visibility:public",
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
        "//visibility:public",
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
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":common",
        ":mocks",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/hash:hash_test",
        "@googletest//:gtest_main",
    ],
)
