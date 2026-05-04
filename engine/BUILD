load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

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

cc_binary(
    name = "hbk-play",
    srcs = ["main.cpp"],
    copts = ["-fexceptions"],
    linkstatic = True,
    visibility = ["//visibility:public"],
    deps = [
        "//engine/audio:sound",
        "//engine/commands",
        "//engine/core:clip",
        "//engine/core:history",
        "//engine/core:midi",
        "//engine/core:project",
        "//engine/core:track",
        "//engine/ipc",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
    ] + select({
        "@platforms//os:windows": ["//engine/vst3:vst3_host_win32"],
        "@platforms//os:macos": ["//engine/vst3:vst3_host_mac"],
        "//conditions:default": ["//engine/vst3:vst3_host_x11"],
    }),
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
