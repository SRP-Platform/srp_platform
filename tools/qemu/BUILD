load("@srp_platform//tools/qemu/rule:qemu_sim.bzl","run_qemu")

sh_binary(
    name = "network_configure",
    srcs = ["network_configure.sh"],
)

sh_binary(
    name = "remove_network_configure",
    srcs = ["remove_network_configure.sh"],
)

run_qemu(
    name="qemu",
    file_ext4 = ["@qemu_image//:ext4"],
    file_bin = ["@qemu_image//:bin"]
)
