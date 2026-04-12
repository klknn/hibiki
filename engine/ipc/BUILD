load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "tcp",
    srcs = select({
        "@platforms//os:windows": ["tcp_win32.cpp"],
        "//conditions:default": ["tcp_posix.cpp"],
    }),
    hdrs = ["tcp.hpp"],
    linkopts = select({
        "@platforms//os:windows": ["-lws2_32"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "worker_channel",
    srcs = select({
        "@platforms//os:windows": ["worker_channel_win32.cpp"],
        "//conditions:default": ["worker_channel_posix.cpp"],
    }),
    hdrs = [
        "worker_channel.hpp",
        "worker_channel_local.hpp",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": [],
        "//conditions:default": ["-lrt"],
    }),
)

cc_library(
    name = "worker_channel_tcp",
    srcs = ["worker_channel_tcp.cpp"],
    hdrs = [
        "worker_channel.hpp",
        "worker_channel_tcp.hpp",
    ],
    deps = [":tcp"],
)

cc_library(
    name = "ipc",
    srcs = ["ipc.cpp"],
    hdrs = ["ipc.hpp"],
    deps = [
        "//engine/plugin:iplugin",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
    ],
)

cc_test(
    name = "ipc_test",
    size = "small",
    timeout = "moderate",
    srcs = ["ipc_test.cc"],
    linkstatic = True,
    deps = [
        ":ipc",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "worker_channel_test",
    target_compatible_with = select({
        # TODO macos support.
        "@platforms//os:macos": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    size = "small",
    srcs = ["worker_channel_test.cc"],
    linkopts = select({
        "@platforms//os:windows": [],
        "@platforms//os:macos": [],
        "//conditions:default": ["-lrt"],
    }),
    deps = [
        ":worker_channel",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "worker_channel_tcp_test",
    size = "small",
    srcs = ["worker_channel_tcp_test.cc"],
    deps = [
        ":worker_channel_tcp",
        "@googletest//:gtest_main",
    ],
)

