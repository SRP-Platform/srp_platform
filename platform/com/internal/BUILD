cc_library(
    name = "internal",
    srcs = ["sd_component.cc"],
    hdrs = ["sd_component.h"],
    visibility = [
        "//bindings/com:__pkg__",
        "//platform/com:__subpackages__",
    ],
    deps = [
        "//platform/com:com_wrapper",
        "//platform/core/model:Model_lib",
    ],
)

cc_library(
    name = "internal_controller",
    srcs = ["sd_controller.cc"],
    hdrs = ["sd_controller.h"],
    visibility = ["//platform/com:__subpackages__"],
    deps = [
        "//bindings/com/ipc/proxy",
        "//bindings/com/ipc/skeleton",
        "//platform/com:com_wrapper",
        "//platform/com/internal",
        "//platform/core/model:Model_lib",
    ],
)
