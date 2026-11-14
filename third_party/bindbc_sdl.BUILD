load("@hibiki//tools/d_rules:defs.bzl", "d_library")

d_library(
    name = "bindbc_sdl",
    srcs = glob(["source/**/*.d"]),
    d_versions = ["SDL_204"],
    includes = ["source"],
    visibility = ["//visibility:public"],
    deps = [
        "@bindbc_common//:bindbc_common",
        "@bindbc_loader//:bindbc_loader",
    ],
)
