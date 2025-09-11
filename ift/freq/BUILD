cc_library(
    name = "common",
    srcs = ["probability_bound.cc"],
    hdrs = ["probability_bound.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "freq",
    srcs = [
        "unicode_frequencies.cc",
        "unigram_probability_calculator.cc",
        "bigram_probability_calculator.cc",
    ],
    hdrs = [
        "noop_probability_calculator.h",
        "unicode_frequencies.h",
        "probability_calculator.h",
        "unigram_probability_calculator.h",
        "bigram_probability_calculator.h",
    ],
    deps = [
        ":common",
        "//ift/encoder:common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@harfbuzz",
    ],
    visibility = ["//visibility:public"],
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
    deps = [
        ":freq",
        "//ift/encoder:common",
    ],
    visibility = ["//visibility:public"],
)
