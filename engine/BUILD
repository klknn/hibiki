load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("//tools/d_rules:defs.bzl", "d_binary", "d_library", "d_test")

d_library(
    name = "bridge_d",
    srcs = ["bridge.d"],
    visibility = ["//visibility:public"],
)

d_library(
    name = "playback_mixer_d",
    srcs = ["playback_mixer.d"],
    betterc = True,
    visibility = ["//visibility:public"],
    deps = ["//engine/core:math_d"],
)

d_test(
    name = "playback_d_test",
    srcs = ["playback_d_test.d"],
    deps = [
        ":playback_mixer_d",
        "//engine/core:math_d",
    ],
)

cc_library(
    name = "test_utils",
    testonly = True,
    hdrs = ["test_utils.hpp"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "test_utils_state",
    testonly = True,
    hdrs = ["test_utils_state.hpp"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "//engine/core:clip",
        "//engine/core:history",
        "//engine/core:midi",
        "//engine/core:project",
        "//engine/core:track",
        "//pb:core_cc_proto",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "builtin_registry",
    srcs = ["builtin_registry.cpp"],
    hdrs = ["builtin_registry.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//engine/effects:registry",
        "//engine/instruments:builtin_drum_machine",
        "//engine/instruments:registry",
        "//engine/plugin:iplugin",
    ],
)

cc_library(
    name = "playback_backend",
    srcs = ["playback_thread.cpp"],
    hdrs = ["playback_thread.hpp"],
    deps = [
        ":playback_mixer_d",
        "//engine/audio:sound",
        "//engine/commands",
        "//engine/core:audio_file",
        "//engine/core:clip",
        "//engine/core:midi",
        "//engine/core:project",
        "//engine/core:track",
        "//engine/effects:builtin_compressor",
        "//engine/effects:builtin_delay",
        "//engine/effects:builtin_envelope_shaper",
        "//engine/effects:builtin_eq",
        "//engine/effects:builtin_hott",
        "//engine/effects:builtin_maxim",
        "//engine/effects:builtin_phaser",
        "//engine/effects:builtin_reverb",
        "//engine/ipc",
        "//pb:commands_cc_proto",
        "//pb:notifications_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ] + select({
        "@platforms//os:windows": ["//engine/vst3:vst3_host_win32"],
        "@platforms//os:macos": ["//engine/vst3:vst3_host_mac"],
        "//conditions:default": ["//engine/vst3:vst3_host_x11"],
    }),
)

d_binary(
    name = "hbk-play",
    srcs = ["main.d"],
    linkopts = select({
        "@platforms//os:macos": [
            "-framework",
            "Cocoa",
            "-framework",
            "CoreAudio",
            "-framework",
            "AudioUnit",
            "-framework",
            "AudioToolbox",
            "-framework",
            "CoreMIDI",
        ],
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lasound",
            "-lpthread",
            "-ldl",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":bridge_d",
        ":playback_backend",
        "//dproto",
        "//engine/commands:commands_d",
        "//pb:commands_d_proto",
        "//pb:notifications_d_proto",
    ],
)

cc_binary(
    name = "hbk-plugin-worker",
    srcs = ["plugin_worker_main.cpp"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//engine/ipc:worker_channel",
        "//engine/vst3:vst3_host",
        "//pb:plugin_worker_cc_proto",
        "@abseil-cpp//absl/log",
    ] + select({
        "@platforms//os:windows": ["//engine/vst3:vst3_host_win32"],
        "@platforms//os:macos": ["//engine/vst3:vst3_host_mac"],
        "//conditions:default": ["//engine/vst3:vst3_host_x11"],
    }),
)

cc_binary(
    name = "hbk-worker-daemon",
    srcs = ["worker_daemon_main.cpp"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    visibility = ["//visibility:public"],
    deps = [
        "//engine/ipc:worker_channel_tcp",
        "//engine/plugin:plugin_scanner",
        "//engine/vst3:vst3_host",
        "//pb:plugin_worker_cc_proto",
        "@abseil-cpp//absl/log",
    ] + select({
        "@platforms//os:windows": ["//engine/vst3:vst3_host_win32"],
        "@platforms//os:macos": ["//engine/vst3:vst3_host_mac"],
        "//conditions:default": ["//engine/vst3:vst3_host_x11"],
    }),
)

cc_test(
    name = "worker_daemon_test",
    size = "large",
    srcs = ["worker_daemon_test.cpp"],
    data = [
        ":hbk-worker-daemon",
        "//testdata",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["-lpthread"],
    }),
    linkstatic = True,
    deps = [
        "//engine/ipc:tcp",
        "//pb:plugin_worker_cc_proto",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
