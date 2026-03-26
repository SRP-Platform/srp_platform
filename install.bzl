load("@srp_platform//bazel/toolchain:toolchain.bzl", "register_all_toolchains")
load("@srp_platform//third_party:deps.bzl", "deps")
load("@srp_platform//bazel/libs:third_party_repositories.bzl", "include_lib","include_qemu_image")


def prepare():
    deps()
    include_lib()
    include_qemu_image()
    register_all_toolchains()