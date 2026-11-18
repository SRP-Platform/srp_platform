load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")
load("@srp_platform//tools/model_generator/ara/mavlink:mavlink.bzl", "ara_mavlink_dialect")

# The model (instances, messages, checks) does not depend on the rest of the generator, so the
# application parser can use it without a dependency cycle.
py_library(
    name = "mavlink_model_lib",
    srcs = ["mavlink_model.py"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = ["@srp_platform//third_party/python:pymavlink"],
)

py_library(
    name = "mavlink_gen_lib",
    srcs = ["mavlink_gen.py"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        ":mavlink_model_lib",
        "@srp_platform//third_party/python:pymavlink",
        "@srp_platform//tools/model_generator/ara/common:common_parser",
    ],
)

py_binary(
    name = "mavlink_gen",
    srcs = ["mavlink_gen.py"],
    main = "mavlink_gen.py",
    visibility = ["//visibility:public"],
    deps = [":mavlink_gen_lib"],
)

# Dialect generated from the test model: proof that mavgen accepts the XML.
ara_mavlink_dialect(
    name = "example_dialect",
    apps = ["testdata/app/app.json"],
    daemon_config = "testdata/app/daemon_config.json",
    data = [
        "testdata/app/service.json",
        "testdata/app/types.json",
    ],
    dialect = "srp_radio",
    visibility = ["@srp_platform//ara/com/mavlink/interop:__pkg__"],  # the codec interop test
)

py_library(
    name = "mavlink_testlib",
    testonly = True,
    srcs = ["mavlink_testlib.py"],
    deps = [":mavlink_gen_lib"],
)

py_test(
    name = "test_mavlink_model",
    srcs = ["test_mavlink_model.py"],
    data = glob(["testdata/**"]),
    main = "test_mavlink_model.py",
    deps = [":mavlink_testlib"],
)

py_test(
    name = "test_mavlink_gen",
    srcs = ["test_mavlink_gen.py"],
    data = glob(["testdata/**"]) + [":example_dialect"],
    main = "test_mavlink_gen.py",
    deps = [":mavlink_testlib"],
)

# The daemon client is linked only into applications that have on: mavlink entries.
ara_runtime_lib(
    name = "runtime_with_mavlink",
    mavlink = True,
    model_src = [
        "testdata/app/app.json",
        "testdata/app/service.json",
        "testdata/app/types.json",
    ],
)

genquery(
    name = "deps_with_mavlink",
    expression = "deps(//tools/model_generator/ara/mavlink:runtime_with_mavlink)",
    scope = [":runtime_with_mavlink"],
)

py_test(
    name = "test_runtime_deps",
    srcs = ["test_runtime_deps.py"],
    data = [
        ":deps_with_mavlink",
        "//tools/model_generator/ara/mavlink/plain_app:deps_without_mavlink",
    ],
    main = "test_runtime_deps.py",
)
