load("@rules_rust//rust:defs.bzl", "rust_binary", "rust_library")

rust_binary(
    name = "ift_graph",
    srcs = glob(include = ["incremental-font-transfer/src/*.rs"]) + [
        "incremental-font-transfer/src/bin/ift_graph.rs",
    ],
    target_compatible_with = select({
        # For some reason rust is currently failing to build on osx,
        # so for now these targets are marked as incompatible.
        # This should be removed once the build failure is resolved.
        # These targets are only needed by a couple of the tests.
        "@platforms//os:osx": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":font_types",
        ":incremental_font_transfer",
        ":read_fonts",
        ":shared_brotli_patch_decoder",
        ":skrifa",
        "@fontations_deps//:clap",
    ],
)

rust_binary(
    name = "ift_extend",
    srcs = glob(include = ["incremental-font-transfer/src/*.rs"]) + [
        "incremental-font-transfer/src/bin/ift_extend.rs",
    ],
    target_compatible_with = select({
        # For some reason rust is currently failing to build on osx,
        # so for now these targets are marked as incompatible.
        # This should be removed once the build failure is resolved.
        # These targets are only needed by a couple of the tests.
        "@platforms//os:osx": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":font_types",
        ":incremental_font_transfer",
        ":klippa",
        ":read_fonts",
        ":skrifa",
        "@fontations_deps//:clap",
        "@fontations_deps//:regex",
    ],
)

rust_library(
    name = "incremental_font_transfer",
    srcs = glob(
        include = ["incremental-font-transfer/src/*.rs"],
        exclude = ["incremental-font-transfer/src/ift_*.rs"],
    ),
    deps = [
        ":font_types",
        ":klippa",
        ":read_fonts",
        ":shared_brotli_patch_decoder",
        ":skrifa",
        ":write_fonts",
        "@fontations_deps//:data-encoding",
        "@fontations_deps//:data-encoding-macro",
    ],
)

rust_library(
    name = "skrifa",
    srcs = glob(include = [
        "skrifa/src/**/*.rs",
        "skrifa/generated/**/*.rs",
    ]),
    crate_features = [
        "std",
        "bytemuck",
    ],
    deps = [
        ":read_fonts",
        "@fontations_deps//:bytemuck",
    ],
)

rust_library(
    name = "klippa",
    srcs = glob(include = ["klippa/src/**/*.rs"]),
    deps = [
        ":skrifa",
        ":write_fonts",
        "@fontations_deps//:fnv",
        "@fontations_deps//:hashbrown",
        "@fontations_deps//:regex",
        "@fontations_deps//:thiserror",
    ],
    # crate_features = ["std", "bytemuck"],
)

rust_library(
    name = "read_fonts",
    srcs = glob(include = [
        "read-fonts/src/**/*.rs",
        "read-fonts/generated/**/*.rs",
    ]),
    crate_features = [
        "bytemuck",
        "std",
        "ift",
    ],
    deps = [
        ":font_types",
        "@fontations_deps//:bytemuck",
    ],
)

rust_library(
    name = "write_fonts",
    srcs = glob(include = [
        "write-fonts/src/**/*.rs",
        "write-fonts/generated/**/*.rs",
    ]),
    crate_features = [
        "bytemuck",
        "std",
        "read",
    ],
    deps = [
        ":font_types",
        ":read_fonts",
        "@fontations_deps//:bytemuck",
        "@fontations_deps//:indexmap",
        "@fontations_deps//:kurbo",
        "@fontations_deps//:log",
    ],
)

rust_library(
    name = "font_types",
    srcs = glob(include = ["font-types/src/**/*.rs"]),
    crate_features = ["bytemuck"],
    deps = [
        "@fontations_deps//:bytemuck",
    ],
)

rust_library(
    name = "shared_brotli_patch_decoder",
    srcs = glob(include = ["shared-brotli-patch-decoder/src/**/*.rs"]),
    crate_features = [
        "bytemuck",
        "c-brotli",
    ],
    deps = [
        "@fontations_deps//:brotlic",
        "@fontations_deps//:brotlic-sys",
        "@fontations_deps//:cfg-if",
    ],
)
