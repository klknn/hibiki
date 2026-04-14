load("@rules_cc//cc:defs.bzl", "cc_library", "objc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "vst3_host",
    srcs = ["vst3_host.cpp"],
    hdrs = [
        "vst3_host.hpp",
        "vst3_host_impl.hpp",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    deps = [
        "//engine/plugin:iplugin",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@vst3sdk",
    ],
)

cc_library(
    name = "vst3_host_x11",
    srcs = ["vst3_host_x11.cpp"],
    linkopts = [
        "-lX11",
        "-lXcursor",
    ],
    target_compatible_with = ["@platforms//os:linux"],
    deps = [
        ":vst3_host",
        "@vst3sdk",
    ],
    alwayslink = True,
)

cc_library(
    name = "vst3_host_win32",
    srcs = ["vst3_host_win32.cpp"],
    linkopts = [
        "-DEFAULTLIB:user32",
        "-DEFAULTLIB:gdi32",
    ],
    target_compatible_with = ["@platforms//os:windows"],
    deps = [
        ":vst3_host",
        "@vst3sdk",
    ],
    alwayslink = True,
)

objc_library(
    name = "vst3_host_mac",
    srcs = ["vst3_host_mac.mm"],
    target_compatible_with = ["@platforms//os:macos"],
    deps = [
        ":vst3_host",
        "@vst3sdk",
    ],
    alwayslink = True,
)

cc_library(
    name = "vst3_host_stub",
    testonly = True,
    srcs = ["vst3_host_stub.cpp"],
    deps = [":vst3_host"],
    alwayslink = True,
)
