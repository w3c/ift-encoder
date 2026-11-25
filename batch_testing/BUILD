load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

py_library(
    name = "run_gen_ift_segmentation_plan_lib",
    srcs = ["run_gen_ift_segmentation_plan.py"],
    data = ["//util:gen_ift_segmentation_plan"],
    imports = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-py//absl:app",
        "@abseil-py//absl/flags",
        "@rules_python//python/runfiles",
    ],
)

py_binary(
    name = "run_gen_ift_segmentation_plan",
    srcs = ["run_gen_ift_segmentation_plan.py"],
    data = ["//util:gen_ift_segmentation_plan"],
    main = "run_gen_ift_segmentation_plan.py",
    imports = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-py//absl:app",
        "@abseil-py//absl/flags",
        "@rules_python//python/runfiles",
    ],
)

py_test(
    name = "run_gen_ift_segmentation_plan_test",
    size = "small",
    srcs = ["run_gen_ift_segmentation_plan_test.py"],
    data = ["//ift/common:testdata/Roboto-Regular.abcd.ttf"],
    deps = [
        ":compare_runs_lib",
        ":run_gen_ift_segmentation_plan_lib",
        "@abseil-py//absl/testing:absltest",
        "@rules_python//python/runfiles",
    ],
)

py_library(
    name = "compare_runs_lib",
    srcs = ["compare_runs.py"],
    data = ["report_template.html"],
    imports = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-py//absl:app",
        "@abseil-py//absl/flags",
        "@rules_python//python/runfiles",
    ],
)

py_binary(
    name = "compare_runs",
    srcs = ["compare_runs.py"],
    data = ["report_template.html"],
    main = "compare_runs.py",
    imports = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-py//absl:app",
        "@abseil-py//absl/flags",
        "@rules_python//python/runfiles",
    ],
)

py_test(
    name = "compare_runs_test",
    size = "small",
    srcs = ["compare_runs_test.py"],
    deps = [
        ":compare_runs_lib",
        "@abseil-py//absl/testing:absltest",
    ],
)
