cc_library(
  name = "proto",
  srcs = [
    "ift_table.h",
    "ift_table.cc",
    "patch_map.h",
    "patch_map.cc",
    "format_2_patch_map.cc",
    "format_2_patch_map.h",
    "patch_encoding.h",
  ],
  visibility = [
    "//util:__pkg__",
    "//ift:__pkg__",
    "//ift/encoder:__pkg__",
  ],
  deps = [
      "//common",
      "@abseil-cpp//absl/status:statusor",
      "@abseil-cpp//absl/container:flat_hash_map",
      "@abseil-cpp//absl/container:flat_hash_set",
      "@harfbuzz",
      "@abseil-cpp//absl/strings",
  ]
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
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "patch_map_test",
    size = "small",
    srcs = [
        "patch_map_test.cc",
        "format_2_patch_map_test.cc",
    ],
    data = [
        "//common:testdata",
        "//ift:testdata",
    ],
    deps = [
        ":proto",
        "//common",
        "@googletest//:gtest_main",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_set",
    ],
)