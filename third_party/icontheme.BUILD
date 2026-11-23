load("@hibiki//tools/d_rules:defs.bzl", "d_library")

d_library(
    name = "icontheme",
    srcs = glob(["source/**/*.d"]),
    includes = ["source"],
    visibility = ["//visibility:public"],
    deps = [
        "@inilike//:inilike",
        "@isfreedesktop//:isfreedesktop",
        "@xdgpaths//:xdgpaths",
    ],
)
