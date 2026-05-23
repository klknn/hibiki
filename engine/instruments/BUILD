load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "adsr",
    srcs = [
        "adsr.cpp",
    ],
    hdrs = [
        "adsr.hpp",
    ],
)

cc_library(
    name = "multi_point_envelope",
    srcs = ["multi_point_envelope.cpp"],
    hdrs = ["multi_point_envelope.hpp"],
)

cc_library(
    name = "builtin_3xosc",
    srcs = ["builtin_3xosc.cpp"],
    hdrs = ["builtin_3xosc.hpp"],
    deps = [
        ":adsr",
        "//engine/core:biquad_filter",
        "//engine/plugin:iplugin",
        "@abseil-cpp//absl/base",
    ],
)

cc_library(
    name = "builtin_acid_bass",
    srcs = ["builtin_acid_bass.cpp"],
    hdrs = ["builtin_acid_bass.hpp"],
    deps = [
        "//engine/plugin:iplugin",
    ],
)

cc_library(
    name = "builtin_dr8_kick",
    srcs = ["builtin_dr8_kick.cpp"],
    hdrs = ["builtin_dr8_kick.hpp"],
    deps = [
        "//engine/plugin:iplugin",
    ],
)

cc_library(
    name = "builtin_organ",
    srcs = ["builtin_organ.cpp"],
    hdrs = ["builtin_organ.hpp"],
    deps = [
        "//engine/plugin:iplugin",
    ],
)

cc_library(
    name = "builtin_sampler",
    srcs = ["builtin_sampler.cpp"],
    hdrs = ["builtin_sampler.hpp"],
    deps = [
        ":adsr",
        "//engine/core:audio_file",
        "//engine/core:biquad_filter",
        "//engine/plugin:iplugin",
    ],
)

cc_library(
    name = "builtin_film",
    srcs = ["builtin_film.cpp"],
    hdrs = ["builtin_film.hpp"],
    deps = [
        ":adsr",
        ":multi_point_envelope",
        "//engine/core:biquad_filter",
        "//engine/plugin:iplugin",
        "@abseil-cpp//absl/base",
    ],
)

cc_library(
    name = "builtin_drum_machine",
    srcs = ["builtin_drum_machine.cpp"],
    hdrs = ["builtin_drum_machine.hpp"],
    deps = [
        ":builtin_3xosc",
        ":builtin_sampler",
        "//engine/ipc",
        "//engine/plugin:iplugin",
        "//pb:commands_cc_proto",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
    ],
)

cc_library(
    name = "instruments",
    deps = [
        ":builtin_3xosc",
        ":builtin_acid_bass",
        ":builtin_dr8_kick",
        ":builtin_drum_machine",
        ":builtin_film",
        ":builtin_organ",
        ":builtin_sampler",
    ],
)

cc_test(
    name = "builtin_3xosc_test",
    srcs = ["builtin_3xosc_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_3xosc",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_sampler_test",
    srcs = ["builtin_sampler_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":builtin_sampler",
        "//engine:test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_film_test",
    srcs = ["builtin_film_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":builtin_film",
        "//engine:test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "multi_point_envelope_test",
    srcs = ["multi_point_envelope_test.cpp"],
    linkstatic = True,
    deps = [
        ":multi_point_envelope",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_drum_machine_test",
    srcs = ["builtin_drum_machine_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":builtin_drum_machine",
        "//engine:test_utils",
        "//engine:test_utils_state",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_acid_bass_test",
    srcs = ["builtin_acid_bass_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_acid_bass",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_organ_test",
    srcs = ["builtin_organ_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_organ",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_dr8_kick_test",
    srcs = ["builtin_dr8_kick_test.cpp"],
    linkstatic = True,
    deps = [
        ":builtin_dr8_kick",
        "@googletest//:gtest_main",
    ],
)
