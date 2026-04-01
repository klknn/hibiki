load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")

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
    deps = [":core_proto"],
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
