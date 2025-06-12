load("@srp_platform//tools/qemu/rule:qemu_sim.bzl","run_qemu")
load("@rules_python//python:defs.bzl", "py_binary")

sh_binary(
    name = "network_configure",
    srcs = ["network_configure.sh"],
)

sh_binary(
    name = "remove_network_configure",
    srcs = ["remove_network_configure.sh"],
)

sh_binary(
    name = "no_root_network_configure",
    srcs = ["no_root_network_configure.sh"],
)

sh_binary(
    name = "no_root_remove_network_configure",
    srcs = ["no_root_remove_network_configure.sh"],
)


run_qemu(
    name="qemu",
    file_ext4 = ["@qemu_image//:ext4"],
    file_bin = ["@qemu_image//:bin"]
)

py_binary(
    name = "wait_for_qemu",
    deps = ["//tools/pytest:core_lib"],
    srcs = ["wait_for_qemu.py"],
    visibility = ["//visibility:public"],
)