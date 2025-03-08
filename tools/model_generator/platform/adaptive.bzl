def platform_json2sm_lib_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_directory("smcomponents.h")
    out2 = ctx.actions.declare_file("initialization.cc")
    args = [out.path, out2.path, ctx.files.src[0].path]

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out, out2],
        arguments = args,
        executable = ctx.executable.tool,
        env = ctx.var,
    )

    return [DefaultInfo(files = depset([out, out2]))]

platform_json2sm_code = rule(
    implementation = platform_json2sm_lib_impl,
    attrs = {
        "src": attr.label(mandatory = False, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:json2sm_code"),
        ),
    },
)

def platform_json2sm_config_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_file("state_config.json")
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

platform_json2sm_config = rule(
    implementation = platform_json2sm_config_impl,
    attrs = {
        "src": attr.label(mandatory = False, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:json2sm_config"),
        ),
    },
)

def platform_json2sm_lib(name, src, visibility = []):
    platform_json2sm_code(
        name = name + ".files",
        src = src,
        visibility = ["//visibility:private"],
    )

    native.cc_library(
        name = name,
        visibility = visibility,
        srcs = [":" + name + ".files"],
        hdrs = ["@srp_platform//platform/exec/sm"],
        includes = ["./smcomponents.h", "."],
        deps = [
            "@srp_platform//platform/exec/sm:sm_interfaces",
            "@srp_platform//platform/exec/sm:state_meager_lib",
        ],
    )
