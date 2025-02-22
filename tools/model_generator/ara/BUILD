load("@rules_python//python:defs.bzl", "py_binary")

# py_binary(
#     name = "json2config",
#     srcs = ["json2config.py"],
#     data = ["@srp_platform//tools/model_generator/ara:example_res"],
#     visibility = ["//visibility:public"],
#     deps = [
#         "@srp_platform//tools/model_generator/ara/common:common_parser",
#     ],
# )

py_binary(
    name = "json2model",
    srcs = ["json2model.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/common:common_parser",
        "//third_party/python:pyfranca"
    ],
)
py_binary(
    name = "model2com",
    srcs = ["model2com.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/common:common_parser",
        "//third_party/python:pyfranca"
    ],
)

py_binary(
    name = "model2config",
    srcs = ["model2config.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/common:common_parser",
        "//third_party/python:pyfranca"
    ],
)

py_binary(
    name = "model2runtime_env",
    srcs = ["model2runtime_env.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/common:common_parser",
        "//third_party/python:pyfranca"
    ],
)

py_binary(
    name = "data_structure_gen",
    srcs = ["data_structure_gen.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/common:common_parser",
    ],
)

py_binary(
    name = "json2sm_code",
    srcs = ["json2sm_code.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/sm:sm_lib",
    ],
)

py_binary(
    name = "json2sm_code_init",
    srcs = ["json2sm_code.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/sm:sm_lib",
    ],
)

py_binary(
    name = "json2sm_config",
    srcs = ["json2sm_config.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//tools/model_generator/ara/sm:sm_lib",
    ],
)
