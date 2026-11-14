"""Bazel build rules for D language using LDC (d_library, d_binary, d_test)."""

load("@rules_cc//cc:defs.bzl", "CcInfo", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", _CcInfo = "CcInfo")

DInfo = provider(
    doc = "Propagates transitive D sources and import directories.",
    fields = {
        "srcs": "depset of D source files (.d)",
        "import_dirs": "depset of import directories (-I)",
        "string_imports": "depset of string import files (-J)",
        "string_import_dirs": "depset of string import directories (-J)",
        "d_versions": "depset of D version identifiers (-d-version=)",
        "betterc": "bool indicating if library is compiled in -betterC mode",
    },
)

def _is_macos(ctx):
    return ctx.target_platform_has_constraint(
        ctx.attr._macos_constraint[platform_common.ConstraintValueInfo],
    )

def _common_ldc_flags(ctx, import_dirs, string_import_dirs = [], d_versions = []):
    flags = [
        "-oq",
        "-conf=" + ctx.file._ldc_conf.path,
        "-I=" + ctx.file._ldc_conf.dirname + "/../import",
    ]
    if _is_macos(ctx):
        flags.append("-mtriple=arm64-apple-macos11.0")
    if ctx.var.get("COMPILATION_MODE", "") == "opt":
        flags.extend(["-O3", "-release", "-enable-inlining", "-boundscheck=safeonly"])
    if ctx.attr.betterc:
        flags.append("-betterC")
    for imp in import_dirs:
        if imp:
            flags.append("-I=" + imp)
        else:
            flags.append("-I=.")
    for jdir in string_import_dirs:
        flags.append("-J=" + (jdir if jdir else "."))
    for ver in d_versions:
        flags.append("-d-version=" + ver)
    return flags

def _base_dir(ctx):
    pkg_dir = ctx.label.package
    ws_root = ctx.label.workspace_root
    if ws_root and pkg_dir:
        return ws_root + "/" + pkg_dir
    return ws_root or pkg_dir

def _collect_deps(ctx):
    transitive_srcs = []
    base_dir = _base_dir(ctx)
    direct_imports = [".", ctx.bin_dir.path]
    if base_dir:
        direct_imports.append(base_dir)
        direct_imports.append(ctx.bin_dir.path + "/" + base_dir)
    for inc in ctx.attr.includes:
        if base_dir:
            direct_imports.append(base_dir + "/" + inc if inc != "." else base_dir)
            direct_imports.append(ctx.bin_dir.path + "/" + base_dir + ("/" + inc if inc != "." else ""))
        else:
            direct_imports.append(inc)
            direct_imports.append(ctx.bin_dir.path + "/" + inc)
    transitive_imports = [depset(direct_imports)]

    direct_string_import_dirs = []
    for sdir in ctx.attr.string_import_dirs:
        if base_dir:
            direct_string_import_dirs.append(base_dir + "/" + sdir if sdir != "." else base_dir)
        else:
            direct_string_import_dirs.append(sdir)
    transitive_string_imports = [depset(ctx.files.string_imports)]
    transitive_string_import_dirs = [depset(direct_string_import_dirs)]
    transitive_versions = [depset(ctx.attr.d_versions)]
    cc_infos = []

    for dep in ctx.attr.deps:
        if DInfo in dep:
            transitive_srcs.append(dep[DInfo].srcs)
            transitive_imports.append(dep[DInfo].import_dirs)
            transitive_string_imports.append(dep[DInfo].string_imports)
            transitive_string_import_dirs.append(dep[DInfo].string_import_dirs)
            transitive_versions.append(dep[DInfo].d_versions)
        if CcInfo in dep:
            cc_infos.append(dep[CcInfo])

    if not ctx.attr.betterc:
        cc_infos.append(ctx.attr._phobos2[CcInfo])
        cc_infos.append(ctx.attr._druntime[CcInfo])

    return (
        depset(ctx.files.srcs, transitive = transitive_srcs),
        depset(transitive = transitive_imports),
        depset(transitive = transitive_string_imports),
        depset(transitive = transitive_string_import_dirs),
        depset(transitive = transitive_versions),
        cc_infos,
    )

def _d_library_impl(ctx):
    out_lib = ctx.actions.declare_file("lib" + ctx.label.name + ".a")
    all_srcs, all_imports, all_str_imports, all_str_dirs, all_versions, dep_cc_infos = _collect_deps(ctx)

    args = ctx.actions.args()
    args.add("--lib")
    args.add("-of=" + out_lib.path)
    args.add_all(_common_ldc_flags(
        ctx,
        all_imports.to_list(),
        all_str_dirs.to_list(),
        all_versions.to_list(),
    ))
    args.add_all(ctx.attr.copts)
    args.add_all(ctx.files.srcs)

    ctx.actions.run(
        inputs = depset(
            ctx.files.srcs + ctx.files._ldc_files,
            transitive = [all_srcs, all_str_imports],
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
        pic_static_library = out_lib,
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
            string_imports = all_str_imports,
            string_import_dirs = all_str_dirs,
            d_versions = all_versions,
            betterc = ctx.attr.betterc,
        ),
        merged_cc_info,
    ]

def _link_d_executable(ctx, is_test):
    out_bin = ctx.actions.declare_file(ctx.label.name)
    all_srcs, all_imports, all_str_imports, all_str_dirs, all_versions, dep_cc_infos = _collect_deps(ctx)

    args = ctx.actions.args()
    args.add("-of=" + out_bin.path)
    args.add_all(_common_ldc_flags(
        ctx,
        all_imports.to_list(),
        all_str_dirs.to_list(),
        all_versions.to_list(),
    ))
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
                static_lib = lib.static_library if lib.static_library else lib.pic_static_library
                if static_lib and static_lib.path not in seen_libs:
                    seen_libs[static_lib.path] = True
                    lib_inputs.append(static_lib)
                    if static_lib.path.endswith(".a") or static_lib.path.endswith(".o") or static_lib.path.endswith(".obj"):
                        args.add(static_lib.path)
                    else:
                        args.add("-L=" + static_lib.path)
            for flag in linker_input.user_link_flags:
                for subflag in flag.split(" "):
                    if subflag:
                        if _is_macos(ctx) and subflag == "-pthread":
                            continue
                        args.add("-L=" + subflag)

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
            transitive = [all_srcs, all_str_imports],
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
    "string_imports": attr.label_list(allow_files = True),
    "string_import_dirs": attr.string_list(default = []),
    "d_versions": attr.string_list(default = []),
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
