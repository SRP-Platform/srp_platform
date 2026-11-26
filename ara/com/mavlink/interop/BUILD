load("@rules_python//python:defs.bzl", "py_test")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

# Codec interoperability with pymavlink. The package depends on the model generator and on the
# daemon client (through the generated runtime), so it is separate from the codec package.

# The generated initialization.cc of a model with MAVLink instances must compile.
ara_runtime_lib(
    name = "example_runtime",
    mavlink = True,
    model_src = [
        "ut/testdata/app/app.json",
        "ut/testdata/app/service.json",
        "ut/testdata/app/types.json",
    ],
)

# Message descriptions taken from the generated initialization.cc (example_runtime); the codec
# is exercised against a pymavlink dialect generated from the same model.
cc_binary(
    name = "mavlink_interop_tool",
    testonly = True,
    srcs = ["ut/mavlink_interop_tool.cc"],
    deps = [
        ":example_runtime",
        "@srp_platform//ara/com/mavlink:mavlink_core",
    ],
)

py_test(
    name = "test_cpp_interop",
    srcs = ["ut/test_cpp_interop.py"],
    data = [
        ":mavlink_interop_tool",
        "@srp_platform//tools/model_generator/ara/mavlink:example_dialect",
    ],
    main = "ut/test_cpp_interop.py",
    deps = ["@srp_platform//third_party/python:pymavlink"],
)
