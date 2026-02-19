load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "alsa_out",
    srcs = ["alsa_out.cpp"],
    hdrs = ["alsa_out.hpp"],
    linkopts = ["-lasound"],
)

cc_library(
    name = "vst3_host",
    srcs = ["vst3_host.cpp"],
    hdrs = ["vst3_host.hpp", "vst3_editor.hpp"],
    deps = [
        "//third_party:vst3sdk",
    ],
    linkopts = ["-lpthread", "-ldl"],
)

cc_library(
    name = "vst3_host_x11",
    srcs = ["vst3_host_x11.cpp"],
    deps = [
        ":vst3_host",
        "//third_party:vst3sdk",
    ],
    linkopts = ["-lX11"],
    alwayslink = True,
)


cc_binary(
    name = "hbk-play",
    srcs = ["main.cpp"],
    deps = [
        ":alsa_out",
        ":vst3_host",
        ":vst3_host_x11",
        "@midifile//:midifile",
    ],
)


load("@rules_python//python:defs.bzl", "py_binary")

py_binary(
    name = "gui",
    srcs = ["gui.py"],
    main = "gui.py",
    deps = ["@bazel_tools//tools/python/runfiles"],
    data = [
        ":hbk-play",
        "//testdata",
    ],
)



