load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "woff2",
    srcs = glob(
        [
            "src/*.cc",
            "src/*.h",
        ],
        exclude = [
            # Fuzzers
            "src/convert_woff2ttf_fuzzer.cc",
            "src/convert_woff2ttf_fuzzer_new_entry.cc",
            # Executables
            "src/woff2_compress.cc",
            "src/woff2_decompress.cc",
            "src/woff2_info.cc",
        ],
    ),
    hdrs = [
        "include/woff2/decode.h",
        "include/woff2/encode.h",
        "include/woff2/output.h",
    ],
    copts = [
        "-Wno-unused-variable",
        "-Wno-unused-but-set-variable",
        "-Wno-sign-compare",
    ],
    includes = [
        "include/",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@brotli//:brotlidec",
        "@brotli//:brotlienc",
    ],
)
