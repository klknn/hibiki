"""Small, hermetic TypeScript type-check rule for Hibiki's SDK."""

def _ts_check_impl(ctx):
    """Runs the pinned TypeScript compiler and returns its build-info output."""
    node_info = ctx.toolchains["@rules_nodejs//nodejs:toolchain_type"].nodeinfo
    output = ctx.actions.declare_file(ctx.label.name + ".tsbuildinfo")

    args = ctx.actions.args()
    args.add("--project")
    args.add(ctx.file.tsconfig.path)
    args.add("--incremental")
    args.add("--tsBuildInfoFile")
    args.add(output.path)

    ctx.actions.run(
        executable = node_info.node,
        arguments = [ctx.file._tsc.path, args],
        inputs = depset(ctx.files.srcs + [ctx.file.tsconfig] + ctx.files._typescript),
        mnemonic = "TypeScriptCheck",
        outputs = [output],
        progress_message = "Type-checking %{label}",
        tools = [node_info.node],
    )

    return [DefaultInfo(files = depset([output]))]

ts_check = rule(
    implementation = _ts_check_impl,
    attrs = {
        "srcs": attr.label_list(
            allow_files = [".d.ts", ".ts"],
            mandatory = True,
        ),
        "tsconfig": attr.label(
            allow_single_file = [".json"],
            mandatory = True,
        ),
        "_tsc": attr.label(
            allow_single_file = True,
            default = Label("@npm_typescript//:lib/tsc.js"),
        ),
        "_typescript": attr.label(
            default = Label("@npm_typescript//:files"),
        ),
    },
    doc = "Type-check TypeScript sources with Hibiki's pinned compiler.",
    toolchains = ["@rules_nodejs//nodejs:toolchain_type"],
)

def _ts_emit_es5_impl(ctx):
    """Compiles a TypeScript SDK runtime source to a single Rhino-compatible ES5 file."""
    node_info = ctx.toolchains["@rules_nodejs//nodejs:toolchain_type"].nodeinfo
    output = ctx.actions.declare_file(ctx.attr.out)
    args = ctx.actions.args()
    args.add("--target")
    args.add("ES5")
    args.add("--module")
    args.add("none")
    args.add("--lib")
    args.add("ES5")
    args.add("--outFile")
    args.add(output.path)
    args.add_all(ctx.files.srcs)
    ctx.actions.run(
        executable = node_info.node,
        arguments = [ctx.file._tsc.path, args],
        inputs = depset(ctx.files.srcs + ctx.files._typescript),
        mnemonic = "TypeScriptEmitEs5",
        outputs = [output],
        progress_message = "Compiling Rhino-compatible TypeScript %{label}",
        tools = [node_info.node],
    )
    return [DefaultInfo(files = depset([output]))]

ts_emit_es5 = rule(
    implementation = _ts_emit_es5_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = [".d.ts", ".ts"], mandatory = True),
        "out": attr.string(mandatory = True),
        "_tsc": attr.label(
            allow_single_file = True,
            default = Label("@npm_typescript//:lib/tsc.js"),
        ),
        "_typescript": attr.label(default = Label("@npm_typescript//:files")),
    },
    doc = "Compile TypeScript to a single Rhino-compatible ES5 JavaScript output.",
    toolchains = ["@rules_nodejs//nodejs:toolchain_type"],
)

def _ts_emit_declarations_impl(ctx):
    """Emits the ambient TypeScript declaration generated from the public SDK contract."""
    node_info = ctx.toolchains["@rules_nodejs//nodejs:toolchain_type"].nodeinfo
    output = ctx.actions.declare_file(ctx.attr.out)
    args = ctx.actions.args()
    args.add("--target")
    args.add("ES5")
    args.add("--module")
    args.add("system")
    args.add("--declaration")
    args.add("--emitDeclarationOnly")
    args.add("--outFile")
    args.add(output.path)
    args.add(ctx.file.src.path)
    ctx.actions.run(
        executable = node_info.node,
        arguments = [ctx.file._tsc.path, args],
        inputs = depset([ctx.file.src] + ctx.files._typescript),
        mnemonic = "TypeScriptDeclarations",
        outputs = [output],
        progress_message = "Generating TypeScript declarations %{label}",
        tools = [node_info.node],
    )
    return [DefaultInfo(files = depset([output]))]

ts_emit_declarations = rule(
    implementation = _ts_emit_declarations_impl,
    attrs = {
        "src": attr.label(allow_single_file = [".ts"], mandatory = True),
        "out": attr.string(mandatory = True),
        "_tsc": attr.label(
            allow_single_file = True,
            default = Label("@npm_typescript//:lib/tsc.js"),
        ),
        "_typescript": attr.label(default = Label("@npm_typescript//:files")),
    },
    doc = "Generate the ambient declaration from Hibiki's canonical TypeScript contract.",
    toolchains = ["@rules_nodejs//nodejs:toolchain_type"],
)

def _runfiles_path(file):
    """Convert an action path to its location in a runfiles tree."""
    if file.short_path.startswith("../"):
        return file.short_path[3:]
    return file.short_path

def _typescript_repl_compiler_impl(ctx):
    """Creates a runfiles-aware launcher for the GUI's bundled TypeScript compiler."""
    node_info = ctx.toolchains["@rules_nodejs//nodejs:toolchain_type"].nodeinfo
    output = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(
        output = output,
        content = """#!/bin/sh
set -eu
if [ -z \"${{RUNFILES_DIR:-}}\" ]; then
  RUNFILES_DIR=\"${{JAVA_RUNFILES:-$0.runfiles}}\"
fi
if [ ! -d \"${{RUNFILES_DIR}}\" ]; then
  echo \"Bazel runfiles are required to launch the bundled TypeScript compiler\" >&2
  exit 1
fi
exec \"${{RUNFILES_DIR}}/{node}\" \"${{RUNFILES_DIR}}/{tsc}\" \"$@\"
""".format(
            node = _runfiles_path(node_info.node),
            tsc = _runfiles_path(ctx.file._tsc),
        ),
        is_executable = True,
    )
    return [DefaultInfo(
        executable = output,
        files = depset([output]),
        runfiles = ctx.runfiles(files = [node_info.node, ctx.file._tsc] + ctx.files._typescript),
    )]

typescript_repl_compiler = rule(
    implementation = _typescript_repl_compiler_impl,
    attrs = {
        "_tsc": attr.label(
            allow_single_file = True,
            default = Label("@npm_typescript//:lib/tsc.js"),
        ),
        "_typescript": attr.label(
            default = Label("@npm_typescript//:files"),
        ),
    },
    doc = "Creates a runfiles-aware launcher for the pinned TypeScript compiler.",
    executable = True,
    toolchains = ["@rules_nodejs//nodejs:toolchain_type"],
)
