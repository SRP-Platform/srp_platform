load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "diag_lib",
    srcs = [
        "diag_db.py",
        "diag_dtc.py",
        "diag_jobs.py",
        "diag_parser.py",
        "diag_runtime_env.py"
    ],
    visibility = ["@srp_platform//tools/model_generator/platform:__subpackages__"],
    deps = ["@srp_platform//tools/model_generator/platform/common:common_lib"],
)
