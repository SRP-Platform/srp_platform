load("@srp_platform//tools/model_generator/ara:adaptive_application.bzl", "ara_runtime_lib")

# An application without on: mavlink entries: its runtime library must not link the daemon client.
ara_runtime_lib(
    name = "runtime_without_mavlink",
    model_src = [
        "app.json",
        "service.json",
        "types.json",
    ],
)

genquery(
    name = "deps_without_mavlink",
    expression = "deps(//tools/model_generator/ara/mavlink/plain_app:runtime_without_mavlink)",
    scope = [":runtime_without_mavlink"],
    visibility = ["//tools/model_generator/ara/mavlink:__pkg__"],
)
