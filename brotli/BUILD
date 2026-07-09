load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

cc_library(
    name = "encoding",
    srcs = [
        "brotli_bit_buffer.cc",
        "brotli_font_diff.cc",
        "brotli_stream.cc",
        "hmtx_differ.h",
        "loca_differ.h",
        "table_range.h",
    ],
    hdrs = [
        "brotli_bit_buffer.h",
        "brotli_font_diff.h",
        "brotli_stream.h",
        "glyf_differ.h",
        "table_differ.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":shared_brotli_encoder",
        "//ift/common",
        "//ift/common:try",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@brotli//:brotlienc",
        "@harfbuzz",
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
        "@brotli//:brotli_inc",
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
        "//ift/common:testdata",
    ],
    deps = [
        ":encoding",
        "//ift/common",
        "//ift/common:test_font_loader",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@brotli//:brotlienc",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@harfbuzz",
    ],
)
