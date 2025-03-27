cc_library(
    name = "common",
    srcs = [
        "bit_input_buffer.cc",
        "bit_input_buffer.h",
        "bit_output_buffer.cc",
        "bit_output_buffer.h",
        "brotli_binary_diff.cc",
        "brotli_binary_patch.cc",
        "file_font_provider.cc",
        "font_helper.cc",
        "hb_set_unique_ptr.cc",
        "sparse_bit_set.cc",
        "axis_range.cc",
        "indexed_data_reader.h",
        "woff2.cc",
        "compat_id.h",
        "compat_id.cc",
    ],
    hdrs = [
        "binary_diff.h",
        "binary_patch.h",
        "branch_factor.h",
        "brotli_binary_diff.h",
        "brotli_binary_patch.h",
        "file_font_provider.h",
        "font_data.h",
        "font_helper.h",
        "font_helper_macros.h",
        "font_provider.h",
        "hb_set_unique_ptr.h",
        "sparse_bit_set.h",
        "axis_range.h",
        "woff2.h",
        "hasher.h",
        "compat_id.h",
        "try.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//brotli:shared_brotli_encoder",
        "@brotli//:brotlidec",
        "@brotli//:brotlienc",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
        "@woff2",
    ],
    copts = [
        "-DHB_EXPERIMENTAL_API",
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
        "bit_buffer_test.cc",
        "bit_input_buffer_test.cc",
        "bit_output_buffer_test.cc",
        "branch_factor_test.cc",
        "brotli_patching_test.cc",
        "axis_range_test.cc",
        "indexed_data_reader_test.cc",
        "file_font_provider_test.cc",
        "font_helper_test.cc",
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
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/container:btree",
    ],
)
