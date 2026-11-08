"""Bzlmod module extension for provisioning the LDC D compiler toolchain."""

_LDC_VERSION = "1.41.0"

_PLATFORMS = {
    "darwin_aarch64": (
        "https://github.com/ldc-developers/ldc/releases/download/v{v}/ldc2-{v}-osx-arm64.tar.xz".format(v = _LDC_VERSION),
        "157267042f10b047210619314aa719b4f0bf887601e93b1c634aa1ecb3c546e4",
        "ldc2-{v}-osx-arm64".format(v = _LDC_VERSION),
    ),
    "darwin_x86_64": (
        "https://github.com/ldc-developers/ldc/releases/download/v{v}/ldc2-{v}-osx-x86_64.tar.xz".format(v = _LDC_VERSION),
        "5bcff48b63c56a45dbaacdb0c5bddc8ea6be86d4a0c7b2c7c8318e047f721181",
        "ldc2-{v}-osx-x86_64".format(v = _LDC_VERSION),
    ),
    "linux_x86_64": (
        "https://github.com/ldc-developers/ldc/releases/download/v{v}/ldc2-{v}-linux-x86_64.tar.xz".format(v = _LDC_VERSION),
        "4a439457f0fe59e69d02fd6b57549fc3c87ad0f55ad9fb9e42507b6f8e327c8f",
        "ldc2-{v}-linux-x86_64".format(v = _LDC_VERSION),
    ),
    "linux_aarch64": (
        "https://github.com/ldc-developers/ldc/releases/download/v{v}/ldc2-{v}-linux-aarch64.tar.xz".format(v = _LDC_VERSION),
        "1c4b950a13d53379ed4f564366c27ec56d6261e21686880d70c7486b3e8c7ba8",
        "ldc2-{v}-linux-aarch64".format(v = _LDC_VERSION),
    ),
}

_BUILD_CONTENT = """\
load("@rules_cc//cc:defs.bzl", "cc_import")

package(default_visibility = ["//visibility:public"])

exports_files([
    "bin/ldc2",
    "etc/ldc2.conf",
])

filegroup(
    name = "all_files",
    srcs = glob([
        "bin/**",
        "etc/**",
        "import/**",
        "lib/*.a",
        "lib/*.o",
    ], allow_empty = True),
)

cc_import(
    name = "phobos2_ldc",
    static_library = "lib/libphobos2-ldc.a",
)

cc_import(
    name = "druntime_ldc",
    static_library = "lib/libdruntime-ldc.a",
)
"""

def _detect_host_key(rctx):
    os_name = rctx.os.name.lower()
    arch = rctx.os.arch.lower()
    if "mac" in os_name or "darwin" in os_name:
        os_part = "darwin"
    else:
        os_part = "linux"
    if arch in ["aarch64", "arm64"]:
        arch_part = "aarch64"
    else:
        arch_part = "x86_64"
    return os_part + "_" + arch_part

def _ldc_toolchain_repo_impl(rctx):
    local_candidates = []
    env_ldc = rctx.os.environ.get("LDC_PATH", "")
    if env_ldc:
        local_candidates.append(env_ldc)
    home = rctx.os.environ.get("HOME", "")
    if home:
        local_candidates.append(home + "/dlang/ldc-" + _LDC_VERSION)

    used_local = False
    for candidate in local_candidates:
        candidate_path = rctx.path(candidate)
        if candidate_path.exists and candidate_path.get_child("bin").get_child("ldc2").exists:
            for subdir in ["bin", "etc", "import", "lib"]:
                rctx.symlink(candidate_path.get_child(subdir), subdir)
            used_local = True
            break

    if not used_local:
        key = _detect_host_key(rctx)
        if key not in _PLATFORMS:
            fail("Unsupported host platform for LDC toolchain: " + key)
        url, sha256, strip_prefix = _PLATFORMS[key]
        rctx.download_and_extract(
            url = url,
            sha256 = sha256,
            stripPrefix = strip_prefix,
        )

    rctx.file("BUILD.bazel", _BUILD_CONTENT)

ldc_toolchain_repo = repository_rule(
    implementation = _ldc_toolchain_repo_impl,
    environ = ["LDC_PATH", "HOME"],
)

def _ldc_repositories_impl(mctx):
    ldc_toolchain_repo(name = "ldc_toolchain")

ldc_repositories = module_extension(
    implementation = _ldc_repositories_impl,
)
