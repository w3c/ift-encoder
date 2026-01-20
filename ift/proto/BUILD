load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_library(
    name = "proto",
    srcs = [
        "format_2_patch_map.cc",
        "format_2_patch_map.h",
        "ift_table.cc",
        "ift_table.h",
        "patch_encoding.h",
        "patch_map.cc",
        "patch_map.h",
    ],
    visibility = [
        "//ift:__pkg__",
        "//ift/encoder:__pkg__",
        "//util:__pkg__",
    ],
    deps = [
        "//common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@harfbuzz",
    ],
)

cc_test(
    name = "ift_table_test",
    size = "small",
    srcs = [
        "ift_table_test.cc",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":proto",
        "//common",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "patch_map_test",
    size = "small",
    srcs = [
        "format_2_patch_map_test.cc",
        "patch_map_test.cc",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":proto",
        "//common",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@googletest//:gtest_main",
    ],
)
