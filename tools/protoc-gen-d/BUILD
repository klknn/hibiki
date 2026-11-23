load("@rules_cc//cc:defs.bzl", "cc_binary")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "protoc-gen-d",
    srcs = [
        "d_generator.cpp",
        "d_generator.hpp",
    ],
    deps = [
        "@protobuf",
        "@protobuf//src/google/protobuf/compiler:code_generator",
        "@protobuf//src/google/protobuf/compiler:plugin",
    ],
)
