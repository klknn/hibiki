load("@hibiki//tools/d_rules:defs.bzl", "d_library")

_ARSD_MODULES = [
    "bmp.d",
    "characterencodings.d",
    "color.d",
    "core.d",
    "dds.d",
    "dom.d",
    "image.d",
    "imageresize.d",
    "jpeg.d",
    "pcx.d",
    "png.d",
    "svg.d",
    "targa.d",
]

genrule(
    name = "arsd_pkg",
    srcs = _ARSD_MODULES,
    outs = ["arsd/" + m for m in _ARSD_MODULES],
    cmd = "mkdir -p $(@D)/arsd && cp $(SRCS) $(@D)/arsd/",
)

d_library(
    name = "arsd",
    srcs = [":arsd_pkg"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
