load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "iplugin",
    hdrs = ["iplugin.hpp"],
    includes = ["."],
)

cc_library(
    name = "plugin_proxy",
    srcs = ["plugin_proxy.cpp"] + select({
        "@platforms//os:windows": ["plugin_proxy_win32.cpp"],
        "//conditions:default": ["plugin_proxy_posix.cpp"],
    }),
    hdrs = ["plugin_proxy.hpp"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["-lpthread"],
    }),
    deps = [
        ":iplugin",
        "//engine/ipc:worker_channel",
        "//engine/ipc:worker_channel_tcp",
        "//pb:plugin_worker_cc_proto",
    ],
)

cc_library(
    name = "plugin_scanner",
    srcs = ["plugin_scanner.cpp"],
    hdrs = ["plugin_scanner.hpp"],
    deps = [
        ":iplugin",
        "//engine/vst3:vst3_host",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_test(
    name = "plugin_scanner_test",
    size = "medium",
    srcs = ["plugin_scanner_test.cpp"],
    data = ["//testdata"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["-lpthread"],
    }),
    linkstatic = True,
    deps = [
        ":plugin_scanner",
        "//engine/vst3:vst3_host",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)
