load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")

bool_flag(
    name = "harfbuzz_dep_graph",
    build_setting_default = True,
    visibility = ["//visibility:public"],
)

config_setting(
    name = "use_harfbuzz_dep_graph",
    flag_values = {
        ":harfbuzz_dep_graph": "True",
    },
    visibility = ["//visibility:public"],
)
