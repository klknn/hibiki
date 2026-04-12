load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "instruments",
    hdrs = [
        "adsr.hpp",
        "biquad_filter.hpp",
        "builtin_3xosc.hpp",
        "builtin_sampler.hpp",
    ],
    deps = [
        "//engine/core:audio_file",
        "//engine/plugin:iplugin",
    ],
)
