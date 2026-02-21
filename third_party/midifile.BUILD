load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "midifile",
    srcs = glob(["src/*.cpp"]),
    hdrs = glob(["include/*.h"]),
    includes = ["include"],
    copts = ["-fexceptions", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables"],
    visibility = ["//visibility:public"],
)
