def pytest_test(name, srcs, **kwargs):
    # this is only needed for passing srcs
    deps = kwargs.pop("deps", [])
    data = kwargs.pop("data", [])
    native.py_test(
        name = name,
        srcs = srcs + ["//tools/pytest:src"],
        main = "pytest_main.py",
        deps = [
            "@pypi//pytest",
            "//tools/pytest:core_lib",
            "@doipclient"
        ] + deps,
        data = data,
        visibility = ["//visibility:public"],
    )
