load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "status",
    hdrs = ["status.hpp"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "math",
    hdrs = ["math.hpp"],
)

cc_library(
    name = "fft",
    srcs = ["fft.cpp"],
    hdrs = ["fft.hpp"],
    copts = ["-fexceptions"],
    deps = [
        "@pocketfft",
    ],
)

cc_library(
    name = "biquad_filter",
    srcs = ["biquad_filter.cpp"],
    hdrs = ["biquad_filter.hpp"],
)

cc_library(
    name = "resample",
    srcs = ["resample.cpp"],
    hdrs = ["resample.hpp"],
)

cc_library(
    name = "audio_file",
    srcs = ["audio_file.cpp"],
    hdrs = ["audio_file.hpp"],
    includes = ["."],
    deps = [
        ":resample",
        ":status",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/strings",
    ],
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
        ":status",
        "//engine:builtin_registry",
        "//engine/audio:sound",
        "//engine/ipc",
        "//engine/plugin:plugin_proxy",
        "//engine/vst3:vst3_host",
        "//pb:commands_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:absl_log",
    ],
)

cc_library(
    name = "project",
    srcs = ["project.cpp"],
    hdrs = ["project.hpp"],
    deps = [
        ":audio_file",
        ":status",
        ":track",
        "//engine/ipc",
        "//engine/plugin:iplugin",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/strings",
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
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "resample_test",
    srcs = ["resample_test.cpp"],
    linkstatic = True,
    deps = [
        ":resample",
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
        ":history",
        ":project",
        "//engine:test_utils",
        "//engine:test_utils_state",
        "//engine/vst3:vst3_host_stub",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:status_matchers",
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
        ":track",
        "//engine/commands",
        "//engine/ipc",
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
        ":midi",
        ":track",
        "//engine:test_utils",
        "//engine/commands",
        "//engine/ipc",
        "//engine/vst3:vst3_host",
        "//engine/vst3:vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fft_test",
    srcs = ["fft_test.cpp"],
    copts = ["-fexceptions"],
    linkstatic = True,
    deps = [
        ":fft",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "audio_editor",
    srcs = ["audio_editor.cpp"],
    hdrs = ["audio_editor.hpp"],
    copts = ["-fexceptions"],
    visibility = ["//visibility:public"],
    deps = [
        ":audio_file",
        "//engine/audio:sound",
        "@abseil-cpp//absl/status",
        "@pocketfft",
    ],
)

cc_test(
    name = "audio_editor_test",
    srcs = ["audio_editor_test.cpp"],
    copts = ["-fexceptions"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":audio_editor",
        "//engine:test_utils",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)
