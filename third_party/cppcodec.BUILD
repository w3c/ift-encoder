load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "cppcodec",
    srcs = glob([
        "cppcodec/*.hpp",
        "cppcodec/detail/*.hpp",
        "cppcodec/data/*.hpp",
    ]),
    hdrs = glob([
        "cppcodec/*.hpp",
        "cppcodec/detail/*.hpp",
        "cppcodec/data/*.hpp",
    ]),
    includes = [
        "cppcodec",
    ],
    visibility = ["//visibility:public"],
)
