cc_library(
    name = "freq",
    srcs = ["unicode_frequencies.cc"],
    hdrs = ["unicode_frequencies.h"],
    deps = [
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
