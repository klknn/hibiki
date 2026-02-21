load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "vst3sdk",
    srcs = glob([
        "vst3sdk/public.sdk/source/vst/*.cpp",
        "vst3sdk/public.sdk/source/vst/hosting/*.cpp",
        "vst3sdk/public.sdk/source/vst/utility/*.cpp",
        "vst3sdk/public.sdk/source/common/*.cpp",
        "vst3sdk/base/source/*.cpp",
        "vst3sdk/pluginterfaces/**/*.cpp",
    ], exclude = [
        "vst3sdk/public.sdk/source/vst/hosting/vst2wrapper.cpp",
        "vst3sdk/**/*win32*.cpp",
        "vst3sdk/**/*mac*.cpp",
        "vst3sdk/**/*mac*.mm",
        "vst3sdk/**/*vstgui*.cpp",
    ]),
    hdrs = glob([
        "vst3sdk/pluginterfaces/**/*.h",
        "vst3sdk/public.sdk/**/*.h",
        "vst3sdk/public.sdk/**/*.cpp", # Some templates are in cpp files
        "vst3sdk/base/**/*.h",
    ]),
    includes = [
        "vst3sdk",
        "vst3sdk/pluginterfaces",
        "vst3sdk/public.sdk/..",
    ],
    visibility = ["//visibility:public"],
    defines = ["RELEASE"],
    linkopts = ["-lpthread", "-ldl"],
)
