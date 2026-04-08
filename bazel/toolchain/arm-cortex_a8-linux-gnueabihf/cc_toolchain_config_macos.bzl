load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")
load(
    "@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl",
    "feature",
    "flag_group",
    "flag_set",
    "tool_path",
)

all_link_actions = [
    ACTION_NAMES.cpp_link_executable,
    ACTION_NAMES.cpp_link_dynamic_library,
    ACTION_NAMES.cpp_link_nodeps_dynamic_library,
]

all_compile_actions = [
    ACTION_NAMES.assemble,
    ACTION_NAMES.c_compile,
    ACTION_NAMES.clif_match,
    ACTION_NAMES.cpp_compile,
    ACTION_NAMES.cpp_header_parsing,
    ACTION_NAMES.cpp_module_codegen,
    ACTION_NAMES.cpp_module_compile,
    ACTION_NAMES.linkstamp_compile,
    ACTION_NAMES.lto_backend,
    ACTION_NAMES.preprocess_assemble,
]

def _impl(ctx):
    # Główny prefiks dla narzędzi w PATH
    prefix = "/opt/homebrew/bin/arm-linux-gnueabihf-"

    # Ścieżka do sysroot - kluczowa dla cross-kompilacji
    sysroot_path = "/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/arm-unknown-linux-gnueabihf/sysroot"

    base_path = "/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/bin/"
    prefix = base_path + "arm-unknown-linux-gnueabihf-"

    # Ścieżka do katalogu libexec (tam gdzie siedzi cc1plus)
    # Musimy ją podać GCC jawnie przez flagę -B
    gcc_libexec_path = "/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/libexec/gcc/arm-unknown-linux-gnueabihf/15.2.0/"

    tool_paths = [
        tool_path(name = "gcc", path = prefix + "gcc"),
        tool_path(name = "g++", path = prefix + "g++"),
        tool_path(name = "cpp", path = prefix + "cpp"),
        tool_path(name = "ar", path = prefix + "ar"),
        tool_path(name = "nm", path = prefix + "nm"),
        tool_path(name = "ld", path = prefix + "ld"),
        tool_path(name = "as", path = prefix + "as"),
        tool_path(name = "objcopy", path = prefix + "objcopy"),
        tool_path(name = "objdump", path = prefix + "objdump"),
        tool_path(name = "strip", path = prefix + "strip"),
    ]

    default_compiler_flags = feature(
        name = "default_compiler_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = all_compile_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            "-B" + gcc_libexec_path,
                            "-std=c++20",
                            "-pthread",
                        ],
                    ),
                ],
            ),
        ],
    )

    default_linker_flags = feature(
        name = "default_linker_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = all_link_actions,
                flag_groups = [
                    flag_group(
                        flags = [
                            "-B" + gcc_libexec_path,
                            "-Wl,--gc-sections",
                            "-pthread",

                            # WYMUSZENIE LINKOWANIA STATYCZNEGO RUNTIME
                            "-static-libstdc++",
                            "-static-libgcc",

                            # Dodatkowa flaga dla linkera, by preferował archiwum .a nad .so
                            "-Wl,-Bstatic",
                            "-lstdc++",
                            "-latomic",
                            "-Wl,-Bdynamic",  # Powrót do dynamicznego dla libc (bezpieczniej)
                            "-Wl,--start-group",
                            "-lm",
                            "-ldl",
                            "-lrt",
                            "-lc",
                            "-lgcc",
                            "-lgcc_eh",
                            "-Wl,--end-group",
                        ],
                    ),
                ],
            ),
        ],
    )

    features = [
        default_compiler_flags,
        default_linker_flags,
    ]

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "bbb-toolchain",
        host_system_name = "local",
        target_system_name = "arm-linux-gnueabihf",
        target_cpu = "armv7",
        target_libc = "unknown",
        compiler = "gcc",
        abi_version = "unknown",
        abi_libc_version = "unknown",
        tool_paths = tool_paths,
        features = features,
        builtin_sysroot = sysroot_path,
        cxx_builtin_include_directories = [
            sysroot_path + "/usr/include",
            "/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/arm-unknown-linux-gnueabihf/include/c++/15.2.0",
            "/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/arm-unknown-linux-gnueabihf/include/c++/15.2.0/arm-unknown-linux-gnueabihf",
            "/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/lib/gcc/arm-unknown-linux-gnueabihf/15.2.0/include",
            "/opt/homebrew/Cellar/arm-unknown-linux-gnueabihf/15.2.0/toolchain/lib/gcc/arm-unknown-linux-gnueabihf/15.2.0/include-fixed",
        ],
    )

cc_toolchain_config_macos = rule(
    implementation = _impl,
    attrs = {},
    provides = [CcToolchainConfigInfo],
)
