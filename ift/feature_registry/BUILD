load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_python//python:defs.bzl", "py_binary")

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
    visibility = [
        "//ift/encoder:__pkg__",
    ],
    deps = [
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@harfbuzz",
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
