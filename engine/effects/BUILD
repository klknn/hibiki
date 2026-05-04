load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "builtin_compressor",
    srcs = ["builtin_compressor.cpp"],
    hdrs = ["builtin_compressor.hpp"],
    deps = ["//engine/plugin:iplugin"],
)

cc_library(
    name = "builtin_delay",
    srcs = ["builtin_delay.cpp"],
    hdrs = ["builtin_delay.hpp"],
    deps = ["//engine/plugin:iplugin"],
)

cc_library(
    name = "builtin_eq",
    srcs = ["builtin_eq.cpp"],
    hdrs = ["builtin_eq.hpp"],
    deps = [
        "//engine/core:biquad_filter",
        "//engine/core:fft",
        "//engine/plugin:iplugin",
    ],
)

cc_library(
    name = "builtin_hott",
    srcs = ["builtin_hott.cpp"],
    hdrs = ["builtin_hott.hpp"],
    deps = [
        ":builtin_compressor",
        "//engine/core:biquad_filter",
        "//engine/plugin:iplugin",
    ],
)

cc_library(
    name = "builtin_limiter",
    srcs = ["builtin_limiter.cpp"],
    hdrs = ["builtin_limiter.hpp"],
    deps = ["//engine/plugin:iplugin"],
)

cc_library(
    name = "builtin_reverb",
    srcs = ["builtin_reverb.cpp"],
    hdrs = ["builtin_reverb.hpp"],
    deps = ["//engine/plugin:iplugin"],
)

cc_library(
    name = "builtin_envelope_shaper",
    srcs = ["builtin_envelope_shaper.cpp"],
    hdrs = ["builtin_envelope_shaper.hpp"],
    deps = ["//engine/plugin:iplugin"],
)

cc_library(
    name = "builtin_phaser",
    srcs = ["builtin_phaser.cpp"],
    hdrs = ["builtin_phaser.hpp"],
    deps = ["//engine/plugin:iplugin"],
)

cc_library(
    name = "builtin_convolver",
    srcs = ["builtin_convolver.cpp"],
    hdrs = ["builtin_convolver.hpp"],
    copts = ["-fexceptions"],
    deps = [
        "//engine/core:audio_file",
        "//engine/plugin:iplugin",
        "@pocketfft",
    ],
)

cc_library(
    name = "effects",
    deps = [
        ":builtin_compressor",
        ":builtin_convolver",
        ":builtin_delay",
        ":builtin_envelope_shaper",
        ":builtin_eq",
        ":builtin_hott",
        ":builtin_limiter",
        ":builtin_phaser",
        ":builtin_reverb",
    ],
)

cc_test(
    name = "builtin_eq_test",
    srcs = ["builtin_eq_test.cpp"],
    copts = ["-fexceptions"],
    linkstatic = True,
    deps = [
        ":builtin_eq",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_compressor_test",
    srcs = ["builtin_compressor_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_compressor",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_delay_test",
    srcs = ["builtin_delay_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_delay",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_reverb_test",
    srcs = ["builtin_reverb_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_reverb",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_limiter_test",
    srcs = ["builtin_limiter_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_limiter",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_hott_test",
    srcs = ["builtin_hott_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_hott",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_envelope_shaper_test",
    srcs = ["builtin_envelope_shaper_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_envelope_shaper",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_phaser_test",
    srcs = ["builtin_phaser_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_phaser",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_convolver_test",
    srcs = ["builtin_convolver_test.cpp"],
    copts = ["-fexceptions"],
    linkstatic = True,
    deps = [
        ":builtin_convolver",
        "@googletest//:gtest_main",
    ],
)
