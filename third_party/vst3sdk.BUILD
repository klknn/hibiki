load("@rules_cc//cc:defs.bzl", "cc_library")

common_excludes = [
    "vstgui4/**",
    "public.sdk/source/vst/hosting/vst2wrapper.cpp",
    "**/*vstgui*.cpp",
]

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
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    copts = select({
        "@platforms//os:windows": ["/EHsc", "/W0"],
        "//conditions:default": ["-fexceptions", "-w"],
    }),
)
