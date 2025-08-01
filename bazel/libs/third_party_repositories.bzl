load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

all_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["//visibility:public"])"""

def include_lib():
    http_archive(
        name = "untar",
        strip_prefix = "untar-main",
        build_file = "@srp_platform//bazel/libs:untar.BUILD",
        urls = ["https://codeload.github.com/AlexisTM/untar/zip/refs/heads/main"],
        type = "zip",
        sha256 = "5b69c01e44ca184bf36d7fa4a19b7bb50d8014a256456e74177f221610865028",
    )

    http_archive(
        name = "doipclient",
        strip_prefix = "python-doipclient-1.1.1/doipclient",
        build_file = "@srp_platform//bazel/libs:doipclient.BUILD",
        urls = ["https://github.com/jacobschaer/python-doipclient/archive/refs/tags/v1.1.1.zip"],
        type = "zip",
    )


def include_qemu_image():
        http_archive(
        name = "qemu_image",
        build_file = "@srp_platform//bazel/libs:qemu_image.BUILD",
        urls = ["https://github.com/SRP-Platform/srp-poky/releases/download/0.2.0/qemu_image_old.zip"],
    )