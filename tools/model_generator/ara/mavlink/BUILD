load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")
load("@srp_platform//tools/model_generator/ara/mavlink:mavlink.bzl", "ara_mavlink_dialect", "ara_mavlink_system")

# The model (instances, messages, checks) does not depend on the rest of the generator, so the
# application parser can use it without a dependency cycle.
py_library(
    name = "mavlink_model_lib",
    srcs = ["mavlink_model.py"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        "@srp_platform//third_party/python:pymavlink",
        "@srp_platform//tools/model_generator/ara/common:model_error_lib",
    ],
)

# The metadata of the ground monitor (<dialect>_monitor.py): what the model says about every element.
py_library(
    name = "monitor_meta_lib",
    srcs = ["monitor_meta.py"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [":mavlink_model_lib"],
)

py_library(
    name = "mavlink_gen_lib",
    srcs = ["mavlink_gen.py"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        ":mavlink_model_lib",
        ":monitor_meta_lib",
        "@srp_platform//third_party/python:pymavlink",
        "@srp_platform//tools/model_generator/ara/common:common_parser",
        "@srp_platform//tools/model_generator/ara/common:model_error_lib",
    ],
)

py_binary(
    name = "mavlink_gen",
    srcs = ["mavlink_gen.py"],
    main = "mavlink_gen.py",
    visibility = ["//visibility:public"],
    deps = [":mavlink_gen_lib"],
)

py_library(
    name = "mavlink_system_lib",
    srcs = ["mavlink_system.py"],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        ":mavlink_gen_lib",
        ":mavlink_model_lib",
        ":monitor_meta_lib",
        "@srp_platform//tools/model_generator/ara/common:model_error_lib",
    ],
)

py_binary(
    name = "mavlink_system_gen",
    srcs = ["mavlink_system.py"],
    main = "mavlink_system.py",
    visibility = ["//visibility:public"],
    deps = [":mavlink_system_lib"],
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

# The second CPU of the test system: another service, the next component_id.
ara_mavlink_dialect(
    name = "example_dialect_cpu2",
    apps = ["testdata/cpu2/app.json"],
    daemon_config = "testdata/cpu2/daemon_config.json",
    data = ["testdata/cpu2/service.json"],
    dialect = "srp_beacon",
)

# One dialect for the station from both CPUs (the daemon configs pair with `cpus` by position).
ara_mavlink_system(
    name = "example_system",
    cpus = [
        ":example_dialect",
        ":example_dialect_cpu2",
    ],
    daemon_configs = [
        "testdata/app/daemon_config.json",
        "testdata/cpu2/daemon_config.json",
    ],
    dialect = "srp_rocket",
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
    name = "test_mavlink_system",
    srcs = ["test_mavlink_system.py"],
    data = glob(["testdata/**"]) + [":example_system"],
    env = {"EXAMPLE_SYSTEM": "$(locations :example_system)"},
    main = "test_mavlink_system.py",
    deps = [
        ":mavlink_system_lib",
        ":mavlink_testlib",
    ],
)

py_test(
    name = "test_mavlink_gen",
    srcs = ["test_mavlink_gen.py"],
    data = glob(["testdata/**"]) + [
        ":example_dialect",
        "@srp_platform//tools/model_generator/ara:json2config",
        "@srp_platform//tools/model_generator/ara:json2runtime_env",
        "@srp_platform//tools/model_generator/ara:json2someip_lib",
    ],
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
