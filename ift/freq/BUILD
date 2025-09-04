cc_library(
    name = "freq",
    srcs = [
        "unicode_frequencies.cc",
        "unigram_probability_calculator.cc",
    ],
    hdrs = [
        "unicode_frequencies.h",
        "probability_calculator.h",
        "unigram_probability_calculator.h",
    ],
    deps = [
        "//ift/encoder",
        "@abseil-cpp//absl/container:flat_hash_map",
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
        "//ift/encoder",
        "@googletest//:gtest_main",
    ],
)
