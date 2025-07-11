genrule(
  name = "feature_registry_h",
  srcs = [
    "@ift_spec//:feature-registry.csv",
  ],
  outs = [
    "feature_registry.h",
  ],
  cmd = "./$(location registry_to_cc) $(location @ift_spec//:feature-registry.csv) > \"$@\"",
  tools = [
    ":registry_to_cc",
  ],
)

cc_library(
  name = "feature_registry",
  srcs = [
    ":feature_registry_h",
  ],
  deps = [
    "@abseil-cpp//absl/container:flat_hash_set",
    "@abseil-cpp//absl/base:no_destructor",
    "@harfbuzz",
  ],
  visibility = [    
    "//ift/encoder:__pkg__",
  ],
)

cc_test(
  name = "feature_registry_test",
  srcs = [
    "feature_registry_test.cc",
  ],
  deps = [
    ":feature_registry",
    "@googletest//:gtest_main",
  ],
)

py_binary(
  name = "registry_to_cc",
  srcs = [
    "registry_to_cc.py",
  ],
)