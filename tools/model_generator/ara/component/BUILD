load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "component",
    srcs = [
        "component.py",
        "component_db.py",
        "component_parser.py",
    ],
    tags = ["no-cache"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        "@srp_platform//tools/model_generator/ara/common:common_lib",
        "@srp_platform//tools/model_generator/ara/common:data_structure_lib",
    ],
)
