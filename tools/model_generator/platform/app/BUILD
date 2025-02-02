load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "app_lib",
    srcs = [
        "adaptive_application.py",
        "adaptive_application_db.py",
        "adaptive_application_parser.py",
        "adaptive_application_extractor.py",
        "adaptive_application_runtime_env.py"
    ],
    visibility = ["@srp_platform//tools/model_generator/platform:__subpackages__"],
    deps = [
        "@srp_platform//tools/model_generator/platform/common:common_lib",
        "@srp_platform//tools/model_generator/platform/common:data_structure_lib",
    ],
)
