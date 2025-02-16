load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "data_structure_lib",
    srcs = [
        "data_structure.py",
        "data_structure_db.py",
        "data_structure_extractor.py",
        "data_structure_parser.py",
    ],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [":common_lib"],
)

py_library(
    name = "common_lib",
    srcs = [
        "singleton_lib.py",
    ],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
)

py_library(
    name = "common_parser",
    srcs = ["common_parser.py"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        "@srp_platform//tools/model_generator/ara/component",
        "@srp_platform//tools/model_generator/ara/common:data_structure_lib",
        "@srp_platform//tools/model_generator/ara/deployment",
        "@srp_platform//tools/model_generator/ara/diag:diag_lib",
        "@srp_platform//tools/model_generator/ara/interface",
    ],
)
