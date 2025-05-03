workspace(name="srp_platform")

load("@srp_platform//:download.bzl", "download")
download()

load("@srp_platform//:install.bzl", "install")
install()

load("@srp_platform//:install_python.bzl", "install_python")
install_python()

load("@srp_platform//:pip_install.bzl", "pip_install")
pip_install()
load("@srp_platform//third_party:deps.bzl", "deps")
deps()

load("@srp_platform//bazel/toolchain:toolchain.bzl", "register_all_toolchains")
register_all_toolchains()
