load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "effects",
    hdrs = [
        "builtin_compressor.hpp",
        "builtin_eq.hpp",
    ],
    copts = ["-fexceptions"],
    deps = [
        "//engine/plugin:iplugin",
        "@pocketfft",
    ],
)
