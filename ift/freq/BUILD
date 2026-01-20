load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

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
        "noop_probability_calculator.h",
        "probability_calculator.h",
        "unicode_frequencies.h",
        "unigram_probability_calculator.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":common",
        "//ift/encoder:common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@harfbuzz",
    ],
)

cc_test(
    name = "unicode_frequencies_test",
    srcs = ["unicode_frequencies_test.cc"],
    deps = [
        ":freq",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "unigram_probability_calculator_test",
    srcs = ["unigram_probability_calculator_test.cc"],
    deps = [
        ":freq",
        "//ift/encoder:common",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "bigram_probability_calculator_test",
    srcs = ["bigram_probability_calculator_test.cc"],
    deps = [
        ":freq",
        "//ift/encoder:common",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "mock_probability_calculator",
    hdrs = ["mock_probability_calculator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":freq",
        "//ift/encoder:common",
    ],
)
