load("@rules_python//python:repositories.bzl", "py_repositories", "python_register_toolchains")
load("@srp_platform//bazel/toolchain:toolchain.bzl", "register_all_toolchains")
load("@srp_platform//third_party:deps.bzl", "deps")

def install():
    register_all_toolchains()
    deps()
    py_repositories()
    python_register_toolchains(
        name = "python_3_11",
        # Available versions are listed in @rules_python//python:versions.bzl.
        # We recommend using the same version your team is already standardized on.
        python_version = "3.11",
    )
