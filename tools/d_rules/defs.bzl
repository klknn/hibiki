"""Bazel build rules for D language using LDC (d_library, d_binary, d_test)."""

load("@rules_cc//cc:defs.bzl", "CcInfo", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", _CcInfo = "CcInfo")

DInfo = provider(
    doc = "Propagates transitive D sources and import directories.",
    fields = {
        "srcs": "depset of D source files (.d)",
        "import_dirs": "depset of import directories (-I)",
        "betterc": "bool indicating if library is compiled in -betterC mode",
    },
)

def _is_macos(ctx):
    return ctx.target_platform_has_constraint(
        ctx.attr._macos_constraint[platform_common.ConstraintValueInfo],
    )

def _common_ldc_flags(ctx, import_dirs):
    flags = [
        "-conf=" + ctx.file._ldc_conf.path,
        "-I=" + ctx.file._ldc_conf.dirname + "/../import",
    ]
    if _is_macos(ctx):
        flags.append("-mtriple=arm64-apple-macos11.0")
    if ctx.attr.betterc:
        flags.append("-betterC")
    for imp in import_dirs:
        if imp:
            flags.append("-I=" + imp)
        else:
            flags.append("-I=.")
    return flags

def _collect_deps(ctx):
    transitive_srcs = []
    pkg_dir = ctx.label.package
    direct_imports = ["."]
    if pkg_dir:
        direct_imports.append(pkg_dir)
    for inc in ctx.attr.includes:
        if pkg_dir:
            direct_imports.append(pkg_dir + "/" + inc if inc != "." else pkg_dir)
        else:
            direct_imports.append(inc)
    transitive_imports = [depset(direct_imports)]
    cc_infos = []

    for dep in ctx.attr.deps:
        if DInfo in dep:
            transitive_srcs.append(dep[DInfo].srcs)
            transitive_imports.append(dep[DInfo].import_dirs)
        if CcInfo in dep:
            cc_infos.append(dep[CcInfo])

    if not ctx.attr.betterc:
        cc_infos.append(ctx.attr._phobos2[CcInfo])
        cc_infos.append(ctx.attr._druntime[CcInfo])

    return (
        depset(ctx.files.srcs, transitive = transitive_srcs),
        depset(transitive = transitive_imports),
        cc_infos,
    )

def _d_library_impl(ctx):
    out_lib = ctx.actions.declare_file("lib" + ctx.label.name + ".a")
    all_srcs, all_imports, dep_cc_infos = _collect_deps(ctx)

    args = ctx.actions.args()
    args.add("--lib")
    args.add("-of=" + out_lib.path)
    args.add_all(_common_ldc_flags(ctx, all_imports.to_list()))
    args.add_all(ctx.attr.copts)
    args.add_all(ctx.files.srcs)

    ctx.actions.run(
        inputs = depset(
            ctx.files.srcs + ctx.files._ldc_files,
            transitive = [all_srcs],
        ),
        outputs = [out_lib],
        executable = ctx.executable._ldc2,
        arguments = [args],
        mnemonic = "DCompileLib",
        progress_message = "Compiling D library %{label}",
        use_default_shell_env = True,
    )

    cc_toolchain = ctx.attr._cc_toolchain[cc_common.CcToolchainInfo]
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )

    library_to_link = cc_common.create_library_to_link(
        actions = ctx.actions,
        feature_configuration = feature_configuration,
        cc_toolchain = cc_toolchain,
        static_library = out_lib,
    )
    linker_input = cc_common.create_linker_input(
        owner = ctx.label,
        libraries = depset([library_to_link]),
    )
    linking_context = cc_common.create_linking_context(
        linker_inputs = depset([linker_input]),
    )
    merged_cc_info = cc_common.merge_cc_infos(
        direct_cc_infos = [CcInfo(linking_context = linking_context)] + dep_cc_infos,
    )

    return [
        DefaultInfo(files = depset([out_lib])),
        DInfo(
            srcs = all_srcs,
            import_dirs = all_imports,
            betterc = ctx.attr.betterc,
        ),
        merged_cc_info,
    ]

def _link_d_executable(ctx, is_test):
    out_bin = ctx.actions.declare_file(ctx.label.name)
    all_srcs, all_imports, dep_cc_infos = _collect_deps(ctx)

    args = ctx.actions.args()
    args.add("-of=" + out_bin.path)
    args.add_all(_common_ldc_flags(ctx, all_imports.to_list()))
    if is_test:
        args.add("-unittest")
        if ctx.attr.main_module:
            args.add("-main")
    args.add_all(ctx.attr.copts)
    args.add_all(ctx.files.srcs)

    lib_inputs = []
    seen_libs = {}
    for cc_info in dep_cc_infos:
        for linker_input in cc_info.linking_context.linker_inputs.to_list():
            for lib in linker_input.libraries:
                if lib.static_library and lib.static_library.path not in seen_libs:
                    seen_libs[lib.static_library.path] = True
                    lib_inputs.append(lib.static_library)
                    args.add(lib.static_library.path)
            for flag in linker_input.user_link_flags:
                args.add("-L=" + flag)

    if _is_macos(ctx):
        args.add("-L=-lc++")
    else:
        args.add("-L=-lstdc++")
        args.add("-L=-lm")
        args.add("-L=-lpthread")

    for linkopt in ctx.attr.linkopts:
        args.add("-L=" + linkopt)

    ctx.actions.run(
        inputs = depset(
            ctx.files.srcs + lib_inputs + ctx.files._ldc_files,
            transitive = [all_srcs],
        ),
        outputs = [out_bin],
        executable = ctx.executable._ldc2,
        arguments = [args],
        mnemonic = "DLinkBin" if not is_test else "DLinkTest",
        progress_message = "Linking D executable %{label}",
        use_default_shell_env = True,
    )

    runfiles = ctx.runfiles(files = ctx.files.data)
    for d in ctx.attr.data:
        runfiles = runfiles.merge(d[DefaultInfo].default_runfiles)
    for dep in ctx.attr.deps:
        runfiles = runfiles.merge(dep[DefaultInfo].default_runfiles)

    return [
        DefaultInfo(
            executable = out_bin,
            files = depset([out_bin]),
            runfiles = runfiles,
        ),
    ]

def _d_binary_impl(ctx):
    return _link_d_executable(ctx, is_test = False)

def _d_test_impl(ctx):
    return _link_d_executable(ctx, is_test = True)

_COMMON_ATTRS = {
    "srcs": attr.label_list(allow_files = [".d"], mandatory = True),
    "deps": attr.label_list(providers = [[DInfo], [CcInfo]]),
    "includes": attr.string_list(default = []),
    "copts": attr.string_list(default = ["-O3", "-release"]),
    "linkopts": attr.string_list(default = []),
    "data": attr.label_list(allow_files = True),
    "betterc": attr.bool(default = False),
    "_ldc2": attr.label(
        default = "@ldc_toolchain//:bin/ldc2",
        allow_single_file = True,
        executable = True,
        cfg = "exec",
    ),
    "_ldc_conf": attr.label(
        default = "@ldc_toolchain//:etc/ldc2.conf",
        allow_single_file = True,
    ),
    "_ldc_files": attr.label(
        default = "@ldc_toolchain//:all_files",
    ),
    "_phobos2": attr.label(
        default = "@ldc_toolchain//:phobos2_ldc",
    ),
    "_druntime": attr.label(
        default = "@ldc_toolchain//:druntime_ldc",
    ),
    "_macos_constraint": attr.label(
        default = "@platforms//os:macos",
    ),
    "_cc_toolchain": attr.label(
        default = "@bazel_tools//tools/cpp:current_cc_toolchain",
    ),
}

d_library = rule(
    implementation = _d_library_impl,
    attrs = _COMMON_ATTRS,
    fragments = ["cpp"],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
)

d_binary = rule(
    implementation = _d_binary_impl,
    attrs = _COMMON_ATTRS,
    executable = True,
    fragments = ["cpp"],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
)

d_test = rule(
    implementation = _d_test_impl,
    attrs = dict(_COMMON_ATTRS.items() + [
        ("main_module", attr.bool(default = True)),
    ]),
    test = True,
    fragments = ["cpp"],
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
)
