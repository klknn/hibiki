load("//tools/d_rules:defs.bzl", "d_library", "d_test")

package(default_visibility = ["//visibility:public"])

d_library(
    name = "wire",
    srcs = ["wire.d"],
    betterc = True,
)

d_library(
    name = "dproto",
    srcs = [
        "proto.d",
        "wire.d",
    ],
)

d_test(
    name = "dproto_test",
    srcs = ["dproto_test.d"],
    deps = [
        ":dproto",
        "//pb:commands_d_proto",
        "//pb:core_d_proto",
        "//pb:notifications_d_proto",
        "//pb:plugin_worker_d_proto",
    ],
)
