load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "deployment",
    srcs = [
        "deployment_db.py",
        "deployment_parser.py",
        "interface.py",
    ],
    tags = ["no-cache"],
    visibility = ["@srp_platform//tools/model_generator/platform:__subpackages__"],
    deps = [
        "@srp_platform//tools/model_generator/platform/common:common_lib",
        "@srp_platform//tools/model_generator/platform/common:data_structure_lib",
    ],
)
