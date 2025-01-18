load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "someip_lib",
    srcs = [
        "service.py",
        "someip_db.py",
        "someip_extractor.py",
        "someip_parser.py",
        "someip_runtime_env.py",
    ],
    visibility = ["//tools/model_generator/ara:__subpackages__"],
    deps = [
        "//tools/model_generator/ara/common:common_lib",
        "//tools/model_generator/ara/common:data_structure_lib",
    ],
    tags = ["no-cache"],
)
