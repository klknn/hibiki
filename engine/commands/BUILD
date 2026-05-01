load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "commands",
    srcs = [
        "automation_commands.cpp",
        "commands.cpp",
        "midi_commands.cpp",
        "modulation_commands.cpp",
        "plugin_commands.cpp",
        "project_commands.cpp",
        "track_commands.cpp",
        "transport_commands.cpp",
    ],
    hdrs = ["commands.hpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//engine/core:audio_file",
        "//engine/core:clip",
        "//engine/core:history",
        "//engine/core:midi",
        "//engine/core:project",
        "//engine/core:status",
        "//engine/core:track",
        "//engine/audio:sound",
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
    linkstatic = True,
    deps = [
        ":commands",
        "//engine/core:track",
        "//engine/ipc",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)
