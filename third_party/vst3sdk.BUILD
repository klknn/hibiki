load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "vst3sdk",
    srcs = glob([
        "public.sdk/source/vst/*.cpp",
        "public.sdk/source/vst/hosting/*.cpp",
        "public.sdk/source/vst/utility/*.cpp",
        "public.sdk/source/common/*.cpp",
        "base/source/*.cpp",
        "pluginterfaces/**/*.cpp",
    ], exclude = [
        "public.sdk/source/vst/hosting/vst2wrapper.cpp",
        "**/*win32*.cpp",
        "**/*mac*.cpp",
        "**/*mac*.mm",
        "**/*vstgui*.cpp",
    ]),
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
    linkopts = ["-lpthread", "-ldl"],
    copts = ["-fexceptions", "-w"],  # DO NOT EDIT. THE ERROR IS UNRELATED TO THIS.
)
