cc_library(
    name = "interfaces",
    hdrs = ["controller_client.h"],
    visibility = ["//bindings:__subpackages__"],
)

cc_library(
    name = "controller",
    srcs = [
        "controller.cc",
        "controller.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//bindings/common:log",
        "//bindings/common/controller:interfaces",
        "//bindings/common/socket:ipc_socket",
    ],
)
