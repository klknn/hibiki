load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools/d_rules:defs.bzl", "d_library", "d_test")

cc_library(
    name = "commands",
    srcs = [
        "audio_editor_commands.cpp",
        "automation_commands.cpp",
        "commands.cpp",
        "dispatcher.cpp",
        "midi_commands.cpp",
        "modulation_commands.cpp",
        "plugin_commands.cpp",
        "project_commands.cpp",
        "track_commands.cpp",
        "transport_commands.cpp",
    ],
    hdrs = [
        "commands.hpp",
        "dispatcher.hpp",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//engine/audio:sound",
        "//engine/core:audio_editor",
        "//engine/core:audio_file",
        "//engine/core:clip",
        "//engine/core:history",
        "//engine/core:midi",
        "//engine/core:project",
        "//engine/core:status",
        "//engine/core:track",
        "//engine/ipc",
        "//engine/ipc:tcp",
        "//engine/plugin:plugin_scanner",
        "//pb:commands_cc_proto",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
        "//pb:plugin_worker_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:absl_log",
        "@protobuf",
    ],
)

cc_test(
    name = "commands_test",
    size = "small",
    srcs = ["commands_test.cc"],
    data = [
        "//testdata",
    ],
    linkstatic = True,
    deps = [
        ":commands",
        "//engine:test_utils",
        "//engine/core:track",
        "//engine/ipc",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

d_library(
    name = "commands_d",
    srcs = ["dispatcher.d"],
    visibility = ["//visibility:public"],
    deps = [
        "//dproto",
        "//engine:bridge_d",
        "//engine/core:history_d",
        "//engine/core:project_d",
        "//engine/ipc:ipc_d",
        "//pb:commands_d_proto",
        "//pb:notifications_d_proto",
    ],
)

d_test(
    name = "commands_d_test",
    srcs = ["commands_d_test.d"],
    deps = [
        ":commands_d",
        "//dproto",
        "//pb:commands_d_proto",
        "//pb:core_d_proto",
        "//pb:notifications_d_proto",
    ],
)
