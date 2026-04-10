load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sound",
    srcs = select({
        "@platforms//os:windows": ["sound_win32.cpp"],
        "@platforms//os:macos": ["sound_coreaudio.cpp"],
        "//conditions:default": ["sound_alsa.cpp", "midi_input_alsa.cpp"],
    }),
    hdrs = ["sound.hpp", "midi_input.hpp"],
    linkopts = select({
        "@platforms//os:windows": ["-DEFAULTLIB:ole32"],
        "@platforms//os:macos": [
            "-framework CoreAudio",
            "-framework AudioUnit",
            "-framework AudioToolbox",
        ],
        "//conditions:default": ["-lasound"],
    }),
    deps = [
        "//engine/plugin:iplugin",
    ],
)
