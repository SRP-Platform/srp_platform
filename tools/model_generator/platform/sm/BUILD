load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "sm_lib",
    srcs = [
        "config_extractor.py",
        "cpp_extractor.py",
        "db.py",
        "sm_parser.py",
        "state.py",
    ],
    visibility = ["@srp_platform//tools/model_generator/platform:__subpackages__"],
    deps = [
        "@srp_platform//tools/model_generator/platform/common:common_lib"
    ],
)
