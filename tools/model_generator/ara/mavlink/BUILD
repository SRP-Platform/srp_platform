load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")
load("@srp_platform//tools/model_generator/ara/mavlink:mavlink.bzl", "ara_mavlink_dialect")

# Rdzeń (walidacja, dialekt, łącza): bez zależności od reszty generatora, żeby
# app_lib i someip_lib mogły z niego korzystać bez cyklu.
py_library(
    name = "mavlink_core_lib",
    srcs = [
        "mavlink_app.py",
        "mavlink_daemon_config.py",
        "mavlink_dialect.py",
        "mavlink_errors.py",
        "mavlink_links.py",
    ],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
)

py_library(
    name = "mavlink_lib",
    srcs = [
        "mavlink_collisions.py",
        "mavlink_gen.py",
    ],
    visibility = ["@srp_platform//tools/model_generator/ara:__subpackages__"],
    deps = [
        ":mavlink_core_lib",
        "@srp_platform//third_party/python:pymavlink",
        "@srp_platform//tools/model_generator/ara/common:common_parser",
    ],
)

py_binary(
    name = "mavlink_gen",
    srcs = ["mavlink_gen.py"],
    main = "mavlink_gen.py",
    visibility = ["//visibility:public"],
    deps = [":mavlink_lib"],
)

py_binary(
    name = "mavgen_run",
    srcs = ["mavgen_run.py"],
    main = "mavgen_run.py",
    visibility = ["//visibility:public"],
    deps = ["@srp_platform//third_party/python:pymavlink"],
)

# Przykładowy dialekt (v1+v2) z modelu testowego: dowód, że mavgen przyjmuje XML.
ara_mavlink_dialect(
    name = "example_dialect",
    apps = ["testdata/app/app.json"],
    daemon_config = "testdata/app/daemon_config.json",
    data = [
        "testdata/app/service.json",
        "testdata/app/types.json",
    ],
    dialect = "srp_radio",
    versions = [
        1,
        2,
    ],
)

py_test(
    name = "test_mavlink_dialect",
    srcs = ["test_mavlink_dialect.py"],
    data = glob(["testdata/*.xml"]),
    main = "test_mavlink_dialect.py",
    deps = [":mavlink_lib"],
)

py_test(
    name = "test_mavlink_app",
    srcs = ["test_mavlink_app.py"],
    data = glob(["testdata/**"]),
    main = "test_mavlink_app.py",
    deps = [":mavlink_lib"],
)

py_test(
    name = "test_mavlink_cpu",
    srcs = ["test_mavlink_cpu.py"],
    data = glob(["testdata/**"]),
    main = "test_mavlink_cpu.py",
    deps = [":mavlink_lib"],
)

py_test(
    name = "test_mavgen_output",
    srcs = ["test_mavgen_output.py"],
    data = [":example_dialect"],
    main = "test_mavgen_output.py",
    deps = ["@srp_platform//third_party/python:pymavlink"],
)

# initialization.cc z modelem łączy MAVLink (łącza bez transportu) musi się kompilować.
ara_runtime_lib(
    name = "example_runtime",
    model_src = [
        "testdata/app/app.json",
        "testdata/app/service.json",
        "testdata/app/types.json",
    ],
)

py_test(
    name = "test_python_reserved_names",
    srcs = [
        "mavgen_run.py",
        "test_mavlink_app.py",
        "test_mavlink_dialect.py",
        "test_python_reserved_names.py",
    ],
    data = glob(["testdata/**"]),
    main = "test_python_reserved_names.py",
    deps = [
        ":mavlink_lib",
        "@srp_platform//third_party/python:pymavlink",
    ],
)

# Narzędzie interopu: opisy wiadomości z wygenerowanego initialization.cc (example_runtime).
cc_binary(
    name = "mavlink_interop_tool",
    testonly = True,
    srcs = ["mavlink_interop_tool.cc"],
    deps = [
        ":example_runtime",
        "@srp_platform//ara/com/mavlink:mavlink_codec",
        "@srp_platform//ara/com/mavlink:mavlink_frame",
        "@srp_platform//ara/com/mavlink:mavlink_signing",
    ],
)

py_test(
    name = "test_cpp_interop",
    srcs = ["test_cpp_interop.py"],
    data = [
        ":example_dialect",
        ":mavlink_interop_tool",
    ],
    main = "test_cpp_interop.py",
    deps = ["@srp_platform//third_party/python:pymavlink"],
)
