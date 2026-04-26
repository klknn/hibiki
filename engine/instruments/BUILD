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
        "//engine/core:biquad_filter",
        "//engine/plugin:iplugin",
        "@abseil-cpp//absl/base",
    ],
)

cc_library(
    name = "instruments",
    deps = [
        ":builtin_3xosc",
        ":builtin_film",
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
        "@googletest//:gtest_main",
    ],
)
