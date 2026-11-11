load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//tools/d_rules:defs.bzl", "d_library")

d_library(
    name = "benchmark_d",
    srcs = ["benchmark_d.d"],
    deps = [
        "//dproto",
        "//engine:playback_mixer_d",
        "//engine/core:biquad_filter_d",
        "//engine/core:history_d",
        "//engine/core:math_d",
        "//engine/effects:builtin_compressor_d",
        "//pb:commands_d_proto",
        "//pb:core_d_proto",
        "//pb:notifications_d_proto",
    ],
)

cc_binary(
    name = "bench",
    srcs = ["benchmark_main.cpp"],
    linkopts = select({
        "@platforms//os:macos": [
            "-lpthread",
            "-ldl",
        ],
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    deps = [
        ":benchmark_d",
        "//engine/core:history",
        "//pb:commands_cc_proto",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
    ],
)
