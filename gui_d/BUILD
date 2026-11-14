load("//tools/d_rules:defs.bzl", "d_binary", "d_library", "d_test")

package(default_visibility = ["//visibility:public"])

d_library(
    name = "gui_model",
    srcs = [
        "ipc_client.d",
        "theme.d",
        "transport_model.d",
    ],
    deps = [
        "//dproto",
        "//pb:commands_d_proto",
        "//pb:core_d_proto",
        "//pb:notifications_d_proto",
    ],
)

d_test(
    name = "gui_d_test",
    srcs = [
        "ipc_client.d",
        "theme.d",
        "transport_model.d",
        "widgets.d",
    ],
    deps = [
        "//dproto",
        "//pb:commands_d_proto",
        "//pb:core_d_proto",
        "//pb:notifications_d_proto",
        "@dlangui",
    ],
)

d_library(
    name = "gui_widgets",
    srcs = ["widgets.d"],
    deps = [
        ":gui_model",
        "@dlangui",
    ],
)

d_binary(
    name = "hibiki_gui",
    srcs = ["main.d"],
    data = ["//engine:hbk-play"],
    linkopts = select({
        "@platforms//os:macos": [
            "-L/opt/homebrew/lib",
            "-framework",
            "Cocoa",
            "-framework",
            "OpenGL",
        ],
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-ldl",
            "-lpthread",
        ],
    }),
    deps = [
        ":gui_model",
        ":gui_widgets",
        "@dlangui",
    ],
)
