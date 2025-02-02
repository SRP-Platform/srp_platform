cc_library(
    name = "com_error_domain",
    srcs = [
        "com_error_domain.cc",
        "someip_error.cc",
    ],
    hdrs = [
        "com_error_domain.h",
        "someip_error.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com/someip:someip_enums",
        "@srp_platform//ara/core:error_lib",
    ],
)

cc_library(
    name = "com_log",
    hdrs = ["log.h"],
    visibility = ["@srp_platform//ara/com:__subpackages__"],
    deps = ["@srp_platform//ara/log"],
)

cc_library(
    name = "com",
    srcs = ["initialization.cc"],
    hdrs = ["initialization.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@srp_platform//ara/com:com_controller",
        "@srp_platform//ara/com/someip/controller",
        "@srp_platform//ara/core:Result",
        "@srp_platform//ara/diag:diagnostic_menager",
    ],
)

cc_library(
    name = "msg_type",
    hdrs = ["msg_type.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "com_controller",
    srcs = ["com_controller.cc"],
    hdrs = [
        "com_controller.h",
        "i_com_client.h",
    ],
    visibility = ["@srp_platform//ara:__subpackages__"],
    deps = [
        "@srp_platform//ara/com:com_error_domain",
        "@srp_platform//ara/com:com_log",
        "@srp_platform//ara/core:Result",
        "@srp_platform//bindings/common/socket:ipc_socket",
    ],
)

cc_library(
    name = "types",
    hdrs = ["types.h"],
    visibility = ["@srp_platform//ara/com:__subpackages__"],
)

cc_library(
    name = "instance_identifier_container",
    hdrs = ["instance_identifier_container.h"],
    visibility = ["//ara/com:__subpackages__"],
    deps = ["//ara/com/model"],
)
