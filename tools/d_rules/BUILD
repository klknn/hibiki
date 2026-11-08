load("@rules_cc//cc:defs.bzl", "cc_test")
load("//tools/d_rules:defs.bzl", "d_library", "d_test")

package(default_visibility = ["//visibility:public"])

exports_files([
    "defs.bzl",
    "extensions.bzl",
])

d_library(
    name = "d_interop_lib",
    srcs = ["d_interop_lib.d"],
    betterc = True,
)

d_test(
    name = "d_rules_test",
    srcs = ["d_rules_test.d"],
    deps = [":d_interop_lib"],
)

cc_test(
    name = "d_interop_cc_test",
    srcs = ["d_interop_test.cpp"],
    linkstatic = True,
    deps = [
        ":d_interop_lib",
        "@googletest//:gtest_main",
    ],
)
