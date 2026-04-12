load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sound",
    srcs = select({
        "@platforms//os:windows": ["sound_win32.cpp", "midi_input_winmm.cpp"],
        "@platforms//os:macos": ["sound_coreaudio.cpp", "midi_input_coremidi.cpp"],
        "//conditions:default": ["sound_alsa.cpp", "midi_input_alsa.cpp"],
    }),
    hdrs = ["sound.hpp", "midi_input.hpp"],
    linkopts = select({
        "@platforms//os:windows": ["-DEFAULTLIB:ole32", "-DEFAULTLIB:winmm"],
        "@platforms//os:macos": [
            "-framework CoreAudio",
            "-framework AudioUnit",
            "-framework AudioToolbox",
            "-framework CoreMIDI",
        ],
        "//conditions:default": ["-lasound"],
    }),
    deps = [
        "//engine/plugin:iplugin",
    ],
)

