load("@bazel_tools//tools/build_defs/pkg:pkg.bzl", "pkg_tar")
load("@srp_platform//tools/common:connect_tar.bzl", "connect_tar")

def _impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_file(ctx.attr.out_name)
    args = [out.path] + [f.path for f in ctx.files.src]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.var,
    )
    return [DefaultInfo(files = depset([out]))]

rename = rule(
    implementation = _impl,
    attrs = {
        "src": attr.label_list(mandatory = False, allow_files = True),
        "out_name": attr.string(),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/common:rename_sh"),
        ),
    },
)

def platform_json2config_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_directory("etc")
    args = [out.path, ctx.files.src[0].path]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.var,
    )

    return [DefaultInfo(files = depset(direct = [out]))]

platform_json2config = rule(
    implementation = platform_json2config_impl,
    attrs = {
        "src": attr.label_list(mandatory = False, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:json2config"),
        ),
    },
)

def json2model_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_file("metadata_model.json")
    args = ["/" + ctx.attr.component_name.replace(".", "/"), out.path, ctx.files.src[0].path]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.var,
    )

    return [DefaultInfo(files = depset([out]))]

json2model = rule(
    implementation = json2model_impl,
    attrs = {
        "src": attr.label_list(mandatory = True, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:json2model"),
        ),
        "component_name": attr.string(mandatory = False, default = "NONE"),
    },
)

def model2runtime_env_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_directory("platform/core")
    args = [out.path, ctx.files.src[0].path]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.var,
    )

    return [DefaultInfo(files = depset([out]))]

_model2runtime_env = rule(
    implementation = model2runtime_env_impl,
    attrs = {
        "src": attr.label(mandatory = True, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:model2runtime_env"),
        ),
    },
)

def model2runtime_env(name, src, visibility = []):
    _model2runtime_env(
        name = name + ".src",
        src = src,
    )
    native.cc_library(
        name = name,
        # deps = ["@srp_platform//platform/core:platform_initialization_lib", "@srp_platform//platform/log"],
        deps = ["@srp_platform//platform/log", "@srp_platform//platform/core:model_db_lib", "@srp_platform//platform/com"],
        srcs = [":" + name + ".src"],
        visibility = visibility,
    )

def model2com_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_directory("com_lib.h")
    args = [out.path, ctx.files.src[0].path]

    # Action to call the script.
    ctx.actions.run(
        inputs = [ctx.files.src[0]],
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.configuration.default_shell_env,
    )

    return [DefaultInfo(files = depset([out]))]

_model2com = rule(
    implementation = model2com_impl,
    attrs = {
        "src": attr.label(mandatory = True, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:model2com"),
        ),
    },
)

def model2com(name, src, visibility = []):
    # Tworzenie unikalnej nazwy dla pliku źródłowego
    _model2com(
        name = name + ".src",
        src = src,
        visibility = ["//visibility:private"],
    )
    native.cc_library(
        name = name,
        deps = ["@srp_platform//platform/com/proxy","//platform/com/skeleton"],
        srcs = [":" + name + ".src"],
        includes = ["./com_lib.h"],
        visibility = visibility,
    )

def model2config_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_directory("etc")
    args = [out.path, ctx.files.src[0].path]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.var,
    )

    return [DefaultInfo(files = depset(direct = [out]))]

_model2config = rule(
    implementation = model2config_impl,
    attrs = {
        "src": attr.label_list(mandatory = False, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:model2config"),
        ),
    },
)

def adaptive_application(name, model_src, bin, etcs = [], visibility = [], tar_path = "opt/"):
    _model2config(
        name = name + "config",
        src = model_src,
        visibility = ["//visibility:public"],
    )
    rename(
        name = "out_bin",
        out_name = name,
        src = [bin],
        visibility = ["//visibility:private"],
    )
    pkg_tar(
        name = "etc-pkg",
        package_dir = tar_path + name,
        srcs = [":" + name + "config"] + etcs,
        visibility = ["//visibility:private"],
    )
    if len(etcs) != 0:
        pkg_tar(
            name = "etc-pkg-other",
            package_dir = tar_path + name + "/etc",
            srcs = etcs,
            visibility = ["//visibility:private"],
        )

    pkg_tar(
        name = "bin-pkg",
        package_dir = tar_path + name + "/bin",
        srcs = [
            ":out_bin",
        ],
        visibility = ["//visibility:private"],
    )
    if len(etcs) != 0:
        connect_tar(
            name = name,
            srcs = [
                ":bin-pkg",
                ":etc-pkg",
                ":etc-pkg-other",
            ],
            visibility = visibility,
        )
    else:
        connect_tar(
            name = name,
            srcs = [
                ":bin-pkg",
                ":etc-pkg",
            ],
            visibility = visibility,
        )
