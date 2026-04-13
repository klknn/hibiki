load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "instruments",
    srcs = [
        "builtin_3xosc.cpp",
        "builtin_sampler.cpp",
    ],
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

cc_test(
    name = "builtin_3xosc_test",
    srcs = ["builtin_3xosc_test.cpp"],
    linkstatic = True,
    deps = [
        ":instruments",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_sampler_test",
    srcs = ["builtin_sampler_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":instruments",
        "//engine:test_utils",
        "@googletest//:gtest_main",
    ],
)
