load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

all_content = """filegroup(name = "all", srcs = glob(["**"]), visibility = ["@srp_platform//visibility:public"])"""

def include_spdlog():
    http_archive(
        name = "com_json",
        strip_prefix = "json-3.11.3",
        urls = ["https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip"],
        sha256 = "04022b05d806eb5ff73023c280b68697d12b93e1b7267a0b22a1a39ec7578069"
    )
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


    