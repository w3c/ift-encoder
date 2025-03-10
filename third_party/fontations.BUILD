load("@rules_rust//rust:defs.bzl", "rust_binary", "rust_library")

rust_binary(
    name = "ift_graph",
    srcs = glob(include = ["incremental-font-transfer/src/*.rs"])
      + ["incremental-font-transfer/src/bin/ift_graph.rs"],
    deps = [
      "@fontations_deps//:clap",
	    ":incremental_font_transfer",
	    ":skrifa",
      ":read_fonts",
      ":font_types",
    ],
    visibility = ["//visibility:public"],
)

rust_binary(
    name = "ift_extend",
    srcs = glob(include = ["incremental-font-transfer/src/*.rs"])
      + ["incremental-font-transfer/src/bin/ift_extend.rs"],
    deps = [
      "@fontations_deps//:clap",
      "@fontations_deps//:regex",
	    ":incremental_font_transfer",
	    ":skrifa",
      ":klippa",
      ":read_fonts",
      ":font_types",
    ],
    visibility = ["//visibility:public"],
)


rust_library(
    name = "incremental_font_transfer",
    srcs = glob(include = ["incremental-font-transfer/src/*.rs"], exclude = ["incremental-font-transfer/src/ift_*.rs"]),
    deps = [
      ":font_types",
      ":read_fonts",
      ":write_fonts",
      ":skrifa",
      ":shared_brotli_patch_decoder",
      ":klippa",
      "@fontations_deps//:data-encoding",
      "@fontations_deps//:data-encoding-macro",
      "@fontations_deps//:uri-template-system",
    ],
)

rust_library(
    name = "skrifa",
    srcs = glob(include = ["skrifa/src/**/*.rs", "skrifa/generated/**/*.rs"]),
    deps = [
      ":read_fonts",
      "@fontations_deps//:bytemuck"
    ],
    crate_features = ["std", "bytemuck"],
)

rust_library(
    name = "klippa",
    srcs = glob(include = ["klippa/src/**/*.rs"]),
    deps = [
      ":skrifa",
      ":write_fonts",
      "@fontations_deps//:fnv",
      "@fontations_deps//:hashbrown",
      "@fontations_deps//:thiserror",
      "@fontations_deps//:regex",
    ],
    # crate_features = ["std", "bytemuck"],
)

rust_library(
    name = "read_fonts",
    srcs = glob(include = ["read-fonts/src/**/*.rs", "read-fonts/generated/**/*.rs"]),
    deps = [
      ":font_types",
      "@fontations_deps//:bytemuck"
    ],
    crate_features = ["bytemuck", "std", "ift"],
)

rust_library(
    name = "write_fonts",
    srcs = glob(include = ["write-fonts/src/**/*.rs", "write-fonts/generated/**/*.rs"]),
    deps = [
      ":read_fonts",
      ":font_types",
      "@fontations_deps//:bytemuck",
      "@fontations_deps//:kurbo",
      "@fontations_deps//:log",
      "@fontations_deps//:indexmap",
    ],
    crate_features = ["bytemuck", "std", "read"],
)

rust_library(
    name = "font_types",
    srcs = glob(include = ["font-types/src/**/*.rs"]),
    deps = [
      "@fontations_deps//:bytemuck"
    ],
    crate_features = ["bytemuck"],
)

rust_library(
    name = "shared_brotli_patch_decoder",
    srcs = glob(include = ["shared-brotli-patch-decoder/src/**/*.rs"]),
    deps = [
      "@fontations_deps//:brotlic",
      "@fontations_deps//:brotlic-sys",
      "@fontations_deps//:cfg-if",
    ],
    crate_features = ["bytemuck", "c-brotli"],
)