load("@hibiki//tools/d_rules:defs.bzl", "d_library")

d_library(
    name = "xdgpaths",
    srcs = glob(["source/**/*.d"]),
    includes = ["source"],
    visibility = ["//visibility:public"],
    deps = [
        "@isfreedesktop//:isfreedesktop",
    ],
)
