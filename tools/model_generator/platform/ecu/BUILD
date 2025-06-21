load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "ecu",
    srcs = [
        "ecu_db.py",
        "ecu_parser.py",
        "network_extractor.py",
    ],
    visibility = ["@srp_platform//tools/model_generator/platform:__subpackages__"],
    deps = ["@srp_platform//tools/model_generator/platform/common:common_lib"],
)
