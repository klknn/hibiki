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
    ],
)
