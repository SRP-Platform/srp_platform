load("@rules_python//python:defs.bzl", "py_binary")

py_binary(
    name = "is_user_allowed",
    deps = ["@pypi//requests"],
    srcs = ["is_user_allowed.py"],
    visibility = ["//visibility:public"],
)