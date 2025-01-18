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
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        "@srp_platform//tools/model_generator/ara/common:common_lib",
        "@srp_platform//tools/model_generator/ara/common:data_structure_lib",
    ],
    tags = ["no-cache"],
)
