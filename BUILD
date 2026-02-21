load("@rules_python//python:defs.bzl", "py_binary", "py_test", "py_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@flatbuffers//:build_defs.bzl", "flatbuffer_cc_library", "flatbuffer_library_public")

cc_library(
    name = "alsa_out",
    srcs = ["alsa_out.cpp"],
    hdrs = ["alsa_out.hpp"],
    linkopts = ["-lasound"],
)

cc_library(
    name = "vst3_host",
    srcs = ["vst3_host.cpp"],
    hdrs = ["vst3_host.hpp", "vst3_host_impl.hpp"],
    deps = [
        "@vst3sdk//:vst3sdk",
    ],
    linkopts = ["-lpthread", "-ldl"],
)

cc_library(
    name = "vst3_host_x11",
    srcs = ["vst3_host_x11.cpp"],
    deps = [
        ":vst3_host",
        "@vst3sdk//:vst3sdk",

    ],
    linkopts = ["-lX11", "-lXcursor"],
    alwayslink = True,
)

cc_binary(
    name = "hbk-play",
    srcs = ["main.cpp"],
    deps = [
        ":alsa_out",
        ":vst3_host",
        ":vst3_host_x11",
        ":hibiki_ipc_cc",
        "@midifile//:midifile",
    ],
)

flatbuffer_cc_library(
    name = "hibiki_ipc_cc",
    srcs = ["hibiki_ipc.fbs"],
)

flatbuffer_library_public(
    name = "hibiki_ipc_py_gen",
    srcs = ["hibiki_ipc.fbs"],
    outs = [
        "hibiki/ipc/__init__.py",
        "hibiki/ipc/Command.py",
        "hibiki/ipc/LoadPlugin.py",
        "hibiki/ipc/LoadClip.py",
        "hibiki/ipc/Play.py",
        "hibiki/ipc/Stop.py",
        "hibiki/ipc/PlayClip.py",
        "hibiki/ipc/StopTrack.py",
        "hibiki/ipc/ShowPluginGui.py",
        "hibiki/ipc/SetParamValue.py",
        "hibiki/ipc/Quit.py",
        "hibiki/ipc/Message.py",
    ],
    language_flag = "--python",
)

py_library(
    name = "hibiki_ipc_py",
    srcs = [":hibiki_ipc_py_gen"],
    deps = ["@pip//flatbuffers:pkg"],
    visibility = ["//visibility:public"],
)

py_binary(
    name = "gui",
    srcs = ["gui.py"],
    main = "gui.py",
    deps = [
        ":hibiki_ipc_py",
        "@bazel_tools//tools/python/runfiles",
    ],
    data = [
        ":hbk-play",
        "//testdata",
    ],
)

py_test(
    name = "gui_type_check",
    srcs = ["mypy_test.py"],
    main = "mypy_test.py",
    deps = [
        ":gui", # This ensures gui.py is available
        "@pip//mypy:pkg",
    ],
    data = ["gui.py"],
)
