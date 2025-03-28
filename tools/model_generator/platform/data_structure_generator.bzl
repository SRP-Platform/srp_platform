def platform_data_structure_generator_impl(ctx):
    # The list of arguments we pass to the script.
    out = ctx.actions.declare_directory("data_structure.h")
    args = [out.path]
    for arg in ctx.files.src:
        args.append(arg.path)

    # Action to call the script.
    ctx.actions.run(
        inputs = ctx.files.src,
        outputs = [out],
        arguments = args,
        executable = ctx.executable.tool,
    )

    return [DefaultInfo(files = depset([out]))]

platform_data_structure_generator = rule(
    implementation = platform_data_structure_generator_impl,
    attrs = {
        "src": attr.label_list(mandatory = False, allow_files = True),
        "tool": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("@srp_platform//tools/model_generator/platform:data_structure_gen"),
        ),
    },
)

def data_structure_generator(name, model = [], visibility = []):
    platform_data_structure_generator(
        name = "data_structure_src",
        src = model,
        visibility = ["//visibility:private"],
    )
    native.cc_library(
        name = name,
        srcs = [":data_structure_src"],
        deps = ["@srp_platform//platform/com:com_error_domain", "@srp_platform//platform/core:Result", "@srp_platform//core/data:data_conv_lib"],
        includes = ["./data_structure.h"],
        visibility = visibility,
    )
