load("@rules_python//python:defs.bzl", "py_binary")

py_binary(
    name = "is_user_allowed",
    deps = ["//third_party/python:requests"],
    srcs = ["is_user_allowed.py"],
    visibility = ["//visibility:public"],
)