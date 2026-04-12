load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "audio_file",
    srcs = ["audio_file.cpp"],
    hdrs = ["audio_file.hpp"],
    includes = ["."],
)

cc_library(
    name = "history",
    srcs = ["history.cpp"],
    hdrs = ["history.hpp"],
)

cc_library(
    name = "midi",
    srcs = ["midi.cpp"],
    hdrs = ["midi.hpp"],
)

cc_library(
    name = "clip",
    srcs = ["clip.cpp"],
    hdrs = ["clip.hpp"],
    deps = [
        ":audio_file",
        ":midi",
        "//pb:core_cc_proto",
    ],
)

cc_library(
    name = "track",
    srcs = ["track.cpp"],
    hdrs = [
        "modulator.hpp",
        "track.hpp",
    ],
    copts = ["-fexceptions"],
    deps = [
        ":audio_file",
        ":clip",
        "//engine/audio:sound",
        "//engine/effects:effects",
        "//engine/instruments:instruments",
        "//engine/ipc:ipc",
        "//engine/plugin:plugin_proxy",
        "//engine/vst3:vst3_host",
        "//pb:commands_cc_proto",
    ],
)

cc_library(
    name = "project",
    srcs = ["project.cpp"],
    hdrs = ["project.hpp"],
    deps = [
        ":audio_file",
        ":track",
        "//engine/ipc:ipc",
        "//engine/plugin:iplugin",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
    ],
)

cc_library(
    name = "commands",
    srcs = ["commands.cpp"],
    hdrs = ["commands.hpp"],
    deps = [
        ":audio_file",
        ":clip",
        ":history",
        ":project",
        ":track",
        "//engine/audio:sound",
        "//engine/ipc:ipc",
        "//engine/ipc:tcp",
        "//engine/core:midi",
        "//engine/plugin:plugin_scanner",
        "//pb:commands_cc_proto",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
        "//pb:plugin_worker_cc_proto",
        "@protobuf//:protobuf",
    ],
)

cc_test(
    name = "audio_file_test",
    srcs = ["audio_file_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":audio_file",
        "//engine:test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "clip_test",
    srcs = ["clip_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":clip",
        "//engine:test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "commands_test",
    size = "small",
    srcs = ["commands_test.cc"],
    linkstatic = True,
    deps = [
        ":commands",
        "//engine/ipc:ipc",
        ":track",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "history_test",
    srcs = ["history_test.cpp"],
    deps = [
        ":history",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "midi_test",
    srcs = ["midi_test.cpp"],
    data = ["//testdata"],
    deps = [
        ":midi",
        "//engine:test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "project_test",
    size = "small",
    srcs = ["project_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":audio_file",
        ":project",
        "//engine:test_utils",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "track_test",
    srcs = ["track_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":track",
        "//engine:test_utils",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "timeline_playback_test",
    size = "small",
    srcs = ["timeline_playback_test.cc"],
    linkstatic = True,
    deps = [
        ":commands",
        ":track",
        "//engine/ipc:ipc",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "integration_test",
    size = "medium",
    srcs = ["integration_test.cc"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":clip",
        ":commands",
        ":midi",
        ":track",
        "//engine:test_utils",
        "//engine/ipc:ipc",
        "//engine/vst3:vst3_host",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)
