load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "encoding",
    srcs = [
        "brotli_bit_buffer.cc",
        "brotli_font_diff.cc",
        "brotli_stream.cc",
        "glyf_differ.h",
        "hmtx_differ.h",
        "loca_differ.h",
        "table_differ.h",
        "table_range.h",
    ],
    hdrs = [
        "brotli_bit_buffer.h",
        "brotli_font_diff.h",
        "brotli_stream.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//common",
        "@abseil-cpp//absl/types:span",
        "@brotli//:brotlienc",
    ],
)

cc_library(
    name = "shared_brotli_encoder",
    hdrs = [
        "shared_brotli_encoder.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@brotli//:brotlienc",
    ],
)

cc_test(
    name = "tests",
    size = "small",
    srcs = [
        "brotli_bit_buffer_test.cc",
        "brotli_font_diff_test.cc",
        "brotli_stream_test.cc",
    ],
    data = [
        "//common:testdata",
    ],
    deps = [
        ":encoding",
        "//common",
        "@abseil-cpp//absl/types:span",
        "@brotli//:brotlienc",
        "@googletest//:gtest_main",
    ],
)
