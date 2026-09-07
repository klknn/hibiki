load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sound",
    srcs = select({
        "@platforms//os:windows": [
            "midi_input_winmm.cpp",
            "sound_win32.cpp",
        ],
        "@platforms//os:macos": [
            "midi_input_coremidi.cpp",
            "sound_coreaudio.cpp",
        ],
        "@platforms//os:android": [
            "midi_input_android.cpp",
            "sound_aaudio.cpp",
        ],
        "//conditions:default": [
            "midi_input_alsa.cpp",
            "sound_alsa.cpp",
        ],
    }),
    hdrs = [
        "midi_input.hpp",
        "sound.hpp",
    ],
    linkopts = select({
        "@platforms//os:windows": [
            "-DEFAULTLIB:ole32",
            "-DEFAULTLIB:winmm",
        ],
        "@platforms//os:macos": [
            "-framework CoreAudio",
            "-framework AudioUnit",
            "-framework AudioToolbox",
            "-framework CoreMIDI",
        ],
        "@platforms//os:android": [
            "-laaudio",
            "-landroid",
            "-llog",
        ],
        "//conditions:default": ["-lasound"],
    }),
    deps = [
        "//engine/plugin:iplugin",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:absl_log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)
