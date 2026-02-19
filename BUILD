load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "hbk-play",
    srcs = glob(["*.cpp", "*.hpp"]),
    deps = [
        "//third_party:vst3sdk",
        "@midifile//:midifile",
    ],
    linkopts = [
        "-lasound",
        "-lpthread",
        "-ldl",
        "-lX11",
        "-lXcursor",
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



