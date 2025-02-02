load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@srp_platform//bazel/libs:third_party_repositories.bzl", "include_lib","include_qemu_image")

def download():
    http_archive(
        name = "com_google_benchmark",
        sha256 = "bdefa4b03c32d1a27bd50e37ca466d8127c1688d834800c38f3c587a396188ee",
        strip_prefix = "benchmark-1.5.3",
        urls = ["https://github.com/google/benchmark/archive/v1.5.3.zip"],
    )

    http_archive(
        name = "com_google_googletest",
        strip_prefix = "googletest-5ab508a01f9eb089207ee87fd547d290da39d015",
        sha256 = "755f9a39bc7205f5a0c428e920ddad092c33c8a1b46997def3f1d4a82aded6e1",
        urls = ["https://github.com/google/googletest/archive/5ab508a01f9eb089207ee87fd547d290da39d015.zip"],
    )
    http_archive(
        name = "rules_python",
        sha256 = "9d04041ac92a0985e344235f5d946f71ac543f1b1565f2cdbc9a2aaee8adf55b",
        strip_prefix = "rules_python-0.26.0",
        url = "https://github.com/bazelbuild/rules_python/releases/download/0.26.0/rules_python-0.26.0.tar.gz",
    )
    include_lib()
    include_qemu_image()