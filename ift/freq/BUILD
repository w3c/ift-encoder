load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

cc_library(
    name = "common",
    srcs = ["probability_bound.cc"],
    hdrs = ["probability_bound.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "freq",
    srcs = [
        "bigram_probability_calculator.cc",
        "unicode_frequencies.cc",
        "unigram_probability_calculator.cc",
    ],
    hdrs = [
        "bigram_probability_calculator.h",
        "lru_cache.h",
        "noop_probability_calculator.h",
        "probability_calculator.h",
        "unicode_frequencies.h",
        "unigram_probability_calculator.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":common",
        "//ift/common",
        "//ift/encoder:common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/hash",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
)

cc_test(
    name = "unicode_frequencies_test",
    srcs = ["unicode_frequencies_test.cc"],
    deps = [
        ":freq",
        "//ift/common",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "unigram_probability_calculator_test",
    srcs = ["unigram_probability_calculator_test.cc"],
    deps = [
        ":common",
        ":freq",
        "//ift/encoder:common",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "bigram_probability_calculator_test",
    srcs = ["bigram_probability_calculator_test.cc"],
    deps = [
        ":common",
        ":freq",
        "//ift/encoder:common",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "mock_probability_calculator",
    hdrs = ["mock_probability_calculator.h"],
    visibility = [
        "//ift/encoder:__pkg__",
    ],
    deps = [
        ":freq",
        "//ift/encoder:common",
    ],
)

cc_test(
    name = "lru_cache_test",
    srcs = ["lru_cache_test.cc"],
    deps = [
        ":freq",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
