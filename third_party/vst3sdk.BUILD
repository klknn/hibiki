load("@rules_cc//cc:defs.bzl", "cc_library", "objc_library")

common_excludes = [
    "vstgui4/**",
    "public.sdk/source/vst/hosting/vst2wrapper.cpp",
    "**/*vstgui*.cpp",
    "public.sdk/source/vst/vstsinglecomponenteffect.cpp",
]


cc_library(
    name = "pluginterfaces",
    hdrs = glob(["pluginterfaces/**/*.h"]),
    srcs = glob(["pluginterfaces/**/*.cpp"]),
    visibility = ["//visibility:public"],
    linkopts = select({
        "@platforms//os:windows": ["ole32.lib"],  # For COM in funknown.h.
        "@platforms//os:osx": [],
        "@platforms//os:linux": [],
    })
)

objc_library(
    name = "public_sdk_mac",
    hdrs = glob(
        ["public.sdk/source/**/*.h"],
    ),
    copts = ["-ObjC++","-std=c++20"],
    srcs = [
        "public.sdk/source/vst/hosting/module_mac.mm",
        "public.sdk/source/common/threadchecker_mac.mm"
    ],
    linkopts = ["-framework Cocoa", "-framework CoreFoundation"],
    deps = [
        ":pluginterfaces",
    ],
)

cc_library(
    name = "vst3sdk",

    srcs = glob(
        [
            "public.sdk/source/vst/*.cpp",
            "public.sdk/source/vst/hosting/*.cpp",
            "public.sdk/source/vst/utility/*.cpp",
            "public.sdk/source/common/*.cpp",
            "base/source/*.cpp",
            "pluginterfaces/**/*.cpp",
        ],
        exclude = common_excludes + [
            "**/*mac*.cpp",
            "**/*mac*.mm",
            "**/*linux*.cpp",
            "**/*win32*.cpp",
        ],
    ) + select({
        "@platforms//os:macos": glob(["**/*mac*.cpp"], exclude = common_excludes),
        "@platforms//os:windows": glob(["**/*win32*.cpp"], exclude = common_excludes),
        "//conditions:default": glob(["**/*linux*.cpp"], exclude = common_excludes),
    }),
    hdrs = glob([
        "pluginterfaces/**/*.h",
        "public.sdk/**/*.h",
        "public.sdk/**/*.cpp", # Some templates are in cpp files
        "base/**/*.h",
    ]),
    includes = [
        "pluginterfaces",
        "public.sdk/..",
    ],
    visibility = ["//visibility:public"],
    defines = ["RELEASE"],
    linkopts = select({
        "@platforms//os:windows": ["ole32.lib"],  # For COM in funknown.h.
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    copts = select({
        "@platforms//os:windows": ["/EHsc", "/W0", "/std:c++17"],
        "//conditions:default": ["-fexceptions", "-w"],
    }),
    deps = select({
        "@platforms//os:macos": [":public_sdk_mac"],
        "//conditions:default": [],
    }),
)
