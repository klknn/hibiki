load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_test")
load("//tools/d_rules:defs.bzl", "d_library")

proto_library(
    name = "core_proto",
    srcs = ["core.proto"],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "commands_proto",
    srcs = ["commands.proto"],
    visibility = ["//visibility:public"],
    deps = [":core_proto"],
)

proto_library(
    name = "notifications_proto",
    srcs = ["notifications.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":commands_proto",
        ":core_proto",
    ],
)

# C++ proto libraries
cc_proto_library(
    name = "core_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":core_proto"],
)

cc_proto_library(
    name = "commands_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":commands_proto"],
)

cc_proto_library(
    name = "notifications_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":notifications_proto"],
)

# Java proto libraries
java_proto_library(
    name = "core_java_proto",
    visibility = ["//visibility:public"],
    deps = [":core_proto"],
)

java_proto_library(
    name = "commands_java_proto",
    visibility = ["//visibility:public"],
    deps = [":commands_proto"],
)

java_proto_library(
    name = "notifications_java_proto",
    visibility = ["//visibility:public"],
    deps = [":notifications_proto"],
)

proto_library(
    name = "plugin_worker_proto",
    srcs = ["plugin_worker.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "plugin_worker_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":plugin_worker_proto"],
)

# D zero-dependency proto libraries
d_library(
    name = "core_d_proto",
    srcs = ["core.d"],
    visibility = ["//visibility:public"],
    deps = ["//dproto"],
)

d_library(
    name = "commands_d_proto",
    srcs = ["commands.d"],
    visibility = ["//visibility:public"],
    deps = [
        ":core_d_proto",
        "//dproto",
    ],
)

d_library(
    name = "notifications_d_proto",
    srcs = ["notifications.d"],
    visibility = ["//visibility:public"],
    deps = [
        ":commands_d_proto",
        ":core_d_proto",
        "//dproto",
    ],
)

d_library(
    name = "plugin_worker_d_proto",
    srcs = ["plugin_worker.d"],
    visibility = ["//visibility:public"],
    deps = ["//dproto"],
)

d_library(
    name = "pb_d_bridge",
    srcs = ["pb_d_bridge.d"],
    deps = [
        ":commands_d_proto",
        ":core_d_proto",
        ":notifications_d_proto",
        ":plugin_worker_d_proto",
        "//dproto",
    ],
)

cc_test(
    name = "pb_d_interop_test",
    srcs = ["pb_d_interop_test.cpp"],
    linkstatic = True,
    deps = [
        ":commands_cc_proto",
        ":core_cc_proto",
        ":notifications_cc_proto",
        ":pb_d_bridge",
        ":plugin_worker_cc_proto",
        "@googletest//:gtest_main",
    ],
)
