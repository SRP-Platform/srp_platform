load("@srp_platform//tools/qemu/rule:qemu_sim.bzl","run_qemu")

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

# Raspberry Pi Zero 2 W (Alpine aarch64) na maszynie virt - obraz budowany
# lokalnie do cache (bez sudo): bazel run @srp_platform//tools/qemu:build_rpi_image
sh_binary(
    name = "build_rpi_image",
    srcs = ["build_rpi_image.sh"],
    data = ["//tools/rpi:provision.sh"],
)

run_qemu(
    name = "qemu_rpi",
    binary = "qemu-system-aarch64",
    machine = "virt",
    cpu = "cortex-a53",
    smp = "4",
    mem = "512",
    disk = "\"$IMG_DIR/disk.img\"",
    kernel = "\"$IMG_DIR/vmlinuz-virt\"",
    initrd = "\"$IMG_DIR/initramfs-virt\"",
    append = "root=/dev/vda rootfstype=ext4 modules=ext4 console=ttyAMA0 net.ifnames=0",
    image_builder = ":build_rpi_image",
    script_name = "run_rpi_script.sh",
)
