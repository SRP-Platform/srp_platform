cc_library(
    name = "instance_specifier",
    srcs = ["instance_specifier.cc"],
    hdrs = ["instance_specifier.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/core:Result",
        "@srp_platform//platform/core:core_error_lib",
        "@srp_platform//platform/core:error_lib",
        "@srp_platform//platform/core:string_view",
    ],
)

cc_library(
    name = "Result",
    hdrs = [
        "result.h",
    ],
    visibility = ["//visibility:public"],
    deps = ["@srp_platform//platform/core:error_lib"],
)

cc_library(
    name = "string_view",
    hdrs = ["string_view.h"],
    visibility = ["@srp_platform//platform:__subpackages__"],
)

cc_library(
    name = "core_error_lib",
    srcs = [
        "core_error_domain.cc",
    ],
    hdrs = [
        "core_error_domain.h",
    ],
    visibility = ["@srp_platform//platform/core:__subpackages__"],
    deps = ["@srp_platform//platform/core:error_lib"],
)

cc_library(
    name = "error_lib",
    srcs = [
        "error_domain.cc",
    ],
    hdrs = [
        "error_code.h",
        "error_domain.h",
        "exception.h",
    ],
    visibility = ["@srp_platform//platform:__subpackages__"],
    deps = ["@srp_platform//platform/core:string_view"],
)

cc_library(
    name = "condition_variable",
    srcs = ["condition_variable.cc"],
    hdrs = [
        "condition_variable.h",
        "condition_variable_type.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/core:core_error_lib",
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//bindings/common/shm:shm_lib",
    ],
)

cc_library(
    name = "platform_initialization_lib",
    hdrs = ["initialization.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//platform/core:Result",
        "@srp_platform//platform/core:model_db_lib",
    ],
)

cc_library(
    name = "model_db_lib",
    srcs = ["model_item.cc"],
    hdrs = [
        "model_db.h",
        "model_item.h",
    ],
    visibility = [
        # "@srp_platform//platform:__subpackages__",
        # "@srp_platform//diag/jobs:__subpackages__",
        "//visibility:public"
    ],
    deps = [
        "@srp_platform//platform/core:core_error_lib",
        "@srp_platform//platform/core:instance_specifier",
        "@srp_platform//platform/core/model:Model_lib",
    ],
)
