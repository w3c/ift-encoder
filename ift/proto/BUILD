load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

package(features = ["layering_check"])

cc_library(
    name = "proto",
    srcs = [
        "format_2_patch_map.cc",
        "ift_table.cc",
        "patch_map.cc",
    ],
    hdrs = [
        "format_2_patch_map.h",
        "ift_table.h",
        "patch_encoding.h",
        "patch_map.h",
    ],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "//ift/common",
        "//ift/common:try",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
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
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":proto",
        "//ift/common",
        "//ift/common:test_font_loader",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest",
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
        "//ift:testdata",
        "//ift/common:testdata",
    ],
    deps = [
        ":proto",
        "//ift/common",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
