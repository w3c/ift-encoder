cc_library(
  name = "encoder",
  srcs = [
    "encoder.h",
    "encoder.cc",
    "glyph_segmentation.h",
    "glyph_segmentation.cc",
    "subset_definition.h",
    "subset_definition.cc",
    "condition.h",
    "condition.cc",
    "closure_glyph_segmenter.h",
    "closure_glyph_segmenter.cc",
  ],
  deps = [
    "//ift/proto",
    "//ift",
    "//util:encoder_config_cc_proto",
    "@abseil-cpp//absl/container:flat_hash_map",
    "@abseil-cpp//absl/container:flat_hash_set",
    "@abseil-cpp//absl/container:btree",
    "@abseil-cpp//absl/log",
    "@abseil-cpp//absl/log:initialize",
    "@harfbuzz",
  ],
  copts = [
    "-DHB_EXPERIMENTAL_API",
  ],
  visibility = [
    "//util:__pkg__",
    "//ift:__pkg__",
    "//ift/client:__pkg__",
    "//js_client:__pkg__",
  ],
)

cc_test(
  name = "encoder_test",
  size = "small",
  srcs = [
    "encoder_test.cc",
  ],
  data = [
    "//ift:testdata",
    "//common:testdata",
  ],
  deps = [
    "//ift/client:fontations",
    "//ift:test_segments",
    ":encoder",
     "@googletest//:gtest_main",
     "//common",
  ],
)

cc_test(
  name = "glyph_segmentation_test",
  size = "small",
  srcs = [
    "glyph_segmentation_test.cc",
  ],
  data = [
    "//ift:testdata",
    "//common:testdata",
  ],
  deps = [
    ":encoder",
     "@googletest//:gtest_main",
     "//common",
  ],
)

cc_test(
  name = "closure_glyph_segmenter_test",
  size = "small",
  srcs = [
    "closure_glyph_segmenter_test.cc",
  ],
  data = [
    "//ift:testdata",
    "//common:testdata",
  ],
  deps = [
    ":encoder",
     "@googletest//:gtest_main",
     "//common",
  ],
)