load("@hibiki//tools/d_rules:defs.bzl", "d_library")

d_library(
    name = "bindbc_freetype",
    srcs = glob(["source/**/*.d"]),
    includes = ["source"],
    visibility = ["//visibility:public"],
    deps = [
        "@bindbc_common//:bindbc_common",
        "@bindbc_loader//:bindbc_loader",
    ],
)
