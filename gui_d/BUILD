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
        "@dlangui",
    ],
)

d_test(
    name = "gui_d_test",
    srcs = [
        "browser_panel.d",
        "channel_rack.d",
        "ipc_client.d",
        "mixer_view.d",
        "piano_roll.d",
        "plugin_window.d",
        "theme.d",
        "timeline_view.d",
        "toolbar.d",
        "transport_model.d",
        "workspace.d",
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
    srcs = [
        "browser_panel.d",
        "channel_rack.d",
        "mixer_view.d",
        "piano_roll.d",
        "plugin_window.d",
        "timeline_view.d",
        "toolbar.d",
        "workspace.d",
    ],
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
