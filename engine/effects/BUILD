load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "effects",
    srcs = [
        "builtin_compressor.cpp",
        "builtin_eq.cpp",
    ],
    hdrs = [
        "builtin_compressor.hpp",
        "builtin_eq.hpp",
    ],
    copts = ["-fexceptions"],
    deps = [
        "//engine/core:fft",
        "//engine/plugin:iplugin",
    ],
)

cc_test(
    name = "builtin_eq_test",
    srcs = ["builtin_eq_test.cpp"],
    copts = ["-fexceptions"],
    linkstatic = True,
    deps = [
        ":effects",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "builtin_compressor_test",
    srcs = ["builtin_compressor_test.cpp"],
    linkstatic = True,
    deps = [
        ":effects",
        "@googletest//:gtest_main",
    ],
)
