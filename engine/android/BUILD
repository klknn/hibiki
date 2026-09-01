load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "hibiki_jni_lib",
    srcs = ["hibiki_jni.cpp"],
    hdrs = ["hibiki_jni.hpp"],
    copts = ["-fexceptions"],
    deps = [
        "//engine:builtin_registry",
        "//engine/audio:sound",
        "//engine/commands",
        "//engine/core:clip",
        "//engine/core:history",
        "//engine/core:midi",
        "//engine/core:project",
        "//engine/core:track",
        "//engine/ipc",
        "//pb:commands_cc_proto",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@bazel_tools//tools/jdk:jni",
    ],
)

cc_binary(
    name = "libhibiki_jni.so",
    linkshared = True,
    deps = [":hibiki_jni_lib"],
)

cc_test(
    name = "hibiki_jni_test",
    srcs = ["hibiki_jni_test.cpp"],
    copts = ["-fexceptions"],
    linkstatic = True,
    deps = [
        ":hibiki_jni_lib",
        "//engine/vst3:vst3_host_stub",
        "//pb:commands_cc_proto",
        "//pb:notifications_cc_proto",
        "@googletest//:gtest_main",
    ],
)
