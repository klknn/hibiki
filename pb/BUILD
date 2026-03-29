load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:java_proto_library.bzl", "java_proto_library")

proto_library(
    name = "core_proto",
    srcs = ["core.proto"],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "commands_proto",
    srcs = ["commands.proto"],
    deps = [":core_proto"],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "notifications_proto",
    srcs = ["notifications.proto"],
    deps = [":core_proto"],
    visibility = ["//visibility:public"],
)

# C++ proto libraries
cc_proto_library(
    name = "core_cc_proto",
    deps = [":core_proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "commands_cc_proto",
    deps = [":commands_proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "notifications_cc_proto",
    deps = [":notifications_proto"],
    visibility = ["//visibility:public"],
)

# Java proto libraries
java_proto_library(
    name = "core_java_proto",
    deps = [":core_proto"],
    visibility = ["//visibility:public"],
)

java_proto_library(
    name = "commands_java_proto",
    deps = [":commands_proto"],
    visibility = ["//visibility:public"],
)

java_proto_library(
    name = "notifications_java_proto",
    deps = [":notifications_proto"],
    visibility = ["//visibility:public"],
)
