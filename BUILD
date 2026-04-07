load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test", "objc_library")
load("@rules_java//java:defs.bzl", "java_binary", "java_library", "java_test")
load("@rules_jvm_external//:defs.bzl", "pom_file")

pom_file(
    name = "pom",
    pom_template = "//third_party:pom_template.xml",
    target = "//:hibiki-gui-java",
)

cc_library(
    name = "sound",
    srcs = select({
        "@platforms//os:windows": ["sound_win32.cpp"],
        "@platforms//os:macos": ["sound_coreaudio.cpp"],
        "//conditions:default": ["sound_alsa.cpp"],
    }),
    hdrs = ["sound.hpp"],
    linkopts = select({
        "@platforms//os:windows": ["-DEFAULTLIB:ole32"],
        "@platforms//os:macos": [
            "-framework CoreAudio",
            "-framework AudioUnit",
            "-framework AudioToolbox",
        ],
        "//conditions:default": ["-lasound"],
    }),
)

cc_library(
    name = "iplugin",
    hdrs = ["iplugin.hpp"],
)

cc_library(
    name = "vst3_host",
    srcs = ["vst3_host.cpp"],
    hdrs = [
        "vst3_host.hpp",
        "vst3_host_impl.hpp",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    deps = [
        ":iplugin",
        "@vst3sdk",
    ],
)

cc_library(
    name = "vst3_host_x11",
    srcs = ["vst3_host_x11.cpp"],
    linkopts = [
        "-lX11",
        "-lXcursor",
    ],
    target_compatible_with = ["@platforms//os:linux"],
    deps = [
        ":vst3_host",
        "@vst3sdk",
    ],
    alwayslink = True,
)

cc_library(
    name = "vst3_host_win32",
    srcs = ["vst3_host_win32.cpp"],
    linkopts = ["-DEFAULTLIB:user32", "-DEFAULTLIB:gdi32"],
    target_compatible_with = ["@platforms//os:windows"],
    deps = [
        ":vst3_host",
        "@vst3sdk",
    ],
    alwayslink = True,
)

objc_library(
    name = "vst3_host_mac",
    srcs = ["vst3_host_mac.mm"],
    target_compatible_with = ["@platforms//os:macos"],
    deps = [
        ":vst3_host",
        "@vst3sdk",
    ],
    alwayslink = True,
)

# Stub implementations of platform-specific Vst3Plugin methods.
# Used only by test binaries that don't link a real platform library.
cc_library(
    name = "vst3_host_stub",
    srcs = ["vst3_host_stub.cpp"],
    deps = [":vst3_host"],
    alwayslink = True,
    testonly = True,
)

cc_library(
    name = "midi",
    srcs = ["midi.cpp"],
    hdrs = ["midi.hpp"],
)

cc_library(
    name = "worker_channel",
    srcs = select({
        "@platforms//os:windows": ["worker_channel_win32.cpp"],
        "//conditions:default": ["worker_channel_posix.cpp"],
    }),
    hdrs = [
        "worker_channel.hpp",
        "worker_channel_local.hpp",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["-lrt"],  # POSIX shared memory
    }),
)

cc_library(
    name = "tcp",
    srcs = select({
        "@platforms//os:windows": ["tcp_win32.cpp"],
        "//conditions:default": ["tcp_posix.cpp"],
    }),
    hdrs = ["tcp.hpp"],
    linkopts = select({
        "@platforms//os:windows": ["-lws2_32"],
        "//conditions:default": [],
    }),
)

cc_library(
    name = "worker_channel_tcp",
    srcs = ["worker_channel_tcp.cpp"],
    hdrs = [
        "worker_channel.hpp",
        "worker_channel_tcp.hpp",
    ],
    deps = [":tcp"],
)

cc_library(
    name = "plugin_proxy",
    srcs = ["plugin_proxy.cpp"] + select({
        "@platforms//os:windows": ["plugin_proxy_win32.cpp"],
        "//conditions:default": ["plugin_proxy_posix.cpp"],
    }),
    hdrs = ["plugin_proxy.hpp"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
        ],
    }),
    deps = [
        ":iplugin",
        ":worker_channel",
        ":worker_channel_tcp",
        "//pb:plugin_worker_cc_proto",
    ],
)

cc_binary(
    name = "hbk-plugin-worker",
    srcs = ["plugin_worker_main.cpp"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    deps = [
        ":vst3_host",
        ":worker_channel",
        "//pb:plugin_worker_cc_proto",
    ] + select({
        "@platforms//os:windows": [
            ":vst3_host_win32",
        ],
        "@platforms//os:macos": [
            ":vst3_host_mac",
        ],
        "//conditions:default": [
            ":vst3_host_x11",
        ],
    }),
)

cc_binary(
    name = "hbk-worker-daemon",
    srcs = ["worker_daemon_main.cpp"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
    deps = [
        ":plugin_scanner",
        ":worker_channel_tcp",
        ":vst3_host",
        "//pb:plugin_worker_cc_proto",
    ] + select({
        "@platforms//os:windows": [
            ":vst3_host_win32",
        ],
        "@platforms//os:macos": [
            ":vst3_host_mac",
        ],
        "//conditions:default": [
            ":vst3_host_x11",
        ],
    }),
)

cc_test(
    name = "worker_channel_test",
    size = "small",
    srcs = ["worker_channel_test.cc"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["-lrt"],
    }),
    deps = [
        ":worker_channel",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "worker_channel_tcp_test",
    size = "small",
    srcs = ["worker_channel_tcp_test.cc"],
    deps = [
        ":worker_channel_tcp",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "track",
    srcs = ["track.cpp"],
    hdrs = ["track.hpp"],
    deps = [
        ":clip",
        ":ipc",
        ":plugin_proxy",
        ":vst3_host",
        "//pb:commands_cc_proto",
    ],
)

cc_library(
    name = "project",
    srcs = ["project.cpp"],
    hdrs = ["project.hpp"],
    deps = [
        ":audio_file",
        ":ipc",
        ":iplugin",
        ":track",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
    ],
)

cc_library(
    name = "ipc",
    srcs = ["ipc.cpp"],
    hdrs = ["ipc.hpp"],
    deps = [
        ":iplugin",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
    ],
)

cc_library(
    name = "history",
    srcs = ["history.cpp"],
    hdrs = ["history.hpp"],
)

cc_test(
    name = "history_test",
    srcs = ["history_test.cpp"],
    deps = [
        ":history",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "plugin_scanner",
    srcs = ["plugin_scanner.cpp"],
    hdrs = ["plugin_scanner.hpp"],
    deps = [
        ":iplugin",
        ":vst3_host",
    ],
)

cc_test(
    name = "plugin_scanner_test",
    size = "medium",
    srcs = ["plugin_scanner_test.cpp"],
    data = ["//testdata"],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
        ],
    }),
    linkstatic = True,
    deps = [
        ":plugin_scanner",
        ":vst3_host",
        ":vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "commands",
    srcs = ["commands.cpp"],
    hdrs = ["commands.hpp"],
    deps = [
        ":clip",
        ":history",
        ":ipc",
        ":midi",
        ":project",
        ":tcp",
        ":track",
        "//pb:commands_cc_proto",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
        "//pb:plugin_worker_cc_proto",
    ],
)

cc_binary(
    name = "hbk-play",
    srcs = [
        "main.cpp",
    ],
    linkstatic = True,
    deps = [
        ":clip",
        ":commands",
        ":history",
        ":ipc",
        ":midi",
        ":project",
        ":sound",
        ":track",
    ] + select({
        "@platforms//os:windows": [
            ":vst3_host_win32",
        ],
        "@platforms//os:macos": [
            ":vst3_host_mac",
        ],
        "//conditions:default": [
            ":vst3_host_x11",
        ],
    }),
)

cc_library(
    name = "test_utils",
    testonly = True,
    hdrs = ["test_utils.hpp"],
)

cc_test(
    name = "midi_test",
    srcs = ["midi_test.cpp"],
    data = ["//testdata"],
    deps = [
        ":midi",
        ":test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "audio_file",
    srcs = ["audio_file.cpp"],
    hdrs = ["audio_file.hpp"],
)

cc_test(
    name = "audio_file_test",
    srcs = ["audio_file_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":audio_file",
        ":test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "clip",
    srcs = ["clip.cpp"],
    hdrs = ["clip.hpp"],
    deps = [
        ":audio_file",
        ":midi",
        "//pb:core_cc_proto",
    ],
)

cc_test(
    name = "clip_test",
    srcs = ["clip_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":clip",
        ":test_utils",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "track_test",
    srcs = ["track_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":test_utils",
        ":track",
        ":vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "worker_daemon_test",
    size = "large",
    srcs = ["worker_daemon_test.cpp"],
    data = [
        ":hbk-worker-daemon",
        "//testdata",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
        ],
    }),
    linkstatic = True,
    deps = [
        ":tcp",
        "//pb:plugin_worker_cc_proto",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "project_test",
    size = "small",
    srcs = ["project_test.cpp"],
    data = ["//testdata"],
    linkstatic = True,
    deps = [
        ":audio_file",
        ":project",
        ":test_utils",
        ":vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "commands_test",
    size = "small",
    srcs = ["commands_test.cc"],
    linkstatic = True,
    deps = [
        ":commands",
        ":ipc",
        ":track",
        ":vst3_host_stub",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "ipc_test",
    size = "small",
    timeout = "moderate",
    srcs = ["ipc_test.cc"],
    linkstatic = True,
    deps = [
        ":ipc",
        "//pb:core_cc_proto",
        "//pb:notifications_cc_proto",
        "@googletest//:gtest",
    ],
)

java_library(
    name = "hibiki-gui-lib",
    srcs = glob(
        ["src/main/java/hibiki/**/*.java"],
        exclude = [
            "src/main/java/hibiki/ClojureMain.java",
            "src/main/java/hibiki/EchoMain.java",
        ],
    ),
    resources = glob(["src/main/resources/**/*"]),
    visibility = ["//visibility:public"],
    deps = [
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@clojure_core_specs_alpha_jar//jar",
        "@clojure_jar//jar",
        "@clojure_spec_alpha_jar//jar",
        "@maven//:com_formdev_flatlaf",
        "@maven//:com_google_protobuf_protobuf_java",
    ],
)

java_binary(
    name = "hibiki-gui-java",
    data = [
        ":hbk-play",
        "//testdata",
    ],
    main_class = "hibiki.GuiMain",
    tags = ["maven_coordinates=com.hibiki:hibiki-gui-java:0.1.0"],
    runtime_deps = [
        ":hibiki-clj-sources",
        ":hibiki-gui-lib",
    ],
)

# Clojure source files — strip src/main/clojure/ so Clojure RT can find them
java_library(
    name = "hibiki-clj-sources",
    resource_strip_prefix = "src/main/clojure",
    resources = glob(["src/main/clojure/**/*.clj"]),
)

# Clojure frontend — shares Java backend classes, adds Clojure sources as resources
java_library(
    name = "hibiki-clj-lib",
    srcs = ["src/main/java/hibiki/ClojureMain.java"],
    deps = [
        ":hibiki-clj-sources",
        ":hibiki-gui-lib",
        "@clojure_core_specs_alpha_jar//jar",
        "@clojure_jar//jar",
        "@clojure_spec_alpha_jar//jar",
    ],
)

java_binary(
    name = "hibiki-gui-clj",
    data = [
        ":hbk-play",
        "//testdata",
    ],
    main_class = "hibiki.ClojureMain",
    runtime_deps = [":hibiki-clj-lib"],
)

# Echo hybrid frontend — Java components + Clojure glue
java_library(
    name = "hibiki-echo-lib",
    srcs = ["src/main/java/hibiki/EchoMain.java"],
    deps = [
        ":hibiki-clj-sources",
        ":hibiki-gui-lib",
        "@clojure_core_specs_alpha_jar//jar",
        "@clojure_jar//jar",
        "@clojure_spec_alpha_jar//jar",
    ],
)

java_binary(
    name = "hibiki-gui-echo",
    data = [
        ":hbk-play",
        "//testdata",
    ],
    main_class = "hibiki.EchoMain",
    runtime_deps = [":hibiki-echo-lib"],
)

java_test(
    name = "backend_manager_test",
    srcs = ["src/test/java/hibiki/BackendManagerTest.java"],
    data = [
        ":hbk-play",
        "//testdata",
    ],
    test_class = "hibiki.BackendManagerTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "plugin_worker_test",
    srcs = ["src/test/java/hibiki/PluginWorkerTest.java"],
    data = [
        ":hbk-play",
        ":hbk-plugin-worker",
        "//testdata",
    ],
    test_class = "hibiki.PluginWorkerTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "theme_test",
    srcs = ["src/test/java/hibiki/ui/ThemeTest.java"],
    test_class = "hibiki.ui.ThemeTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "component_initialization_test",
    srcs = ["src/test/java/hibiki/ui/ComponentTests.java"],
    test_class = "hibiki.ui.ComponentTests",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "timeline_view_test",
    srcs = ["src/test/java/hibiki/ui/TimelineViewTest.java"],
    test_class = "hibiki.ui.TimelineViewTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "remote_editor_panel_test",
    srcs = ["src/test/java/hibiki/ui/RemoteEditorPanelTest.java"],
    test_class = "hibiki.ui.RemoteEditorPanelTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "automation_mouse_handler_test",
    srcs = ["src/test/java/hibiki/ui/AutomationMouseHandlerTest.java"],
    test_class = "hibiki.ui.AutomationMouseHandlerTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "timeline_mouse_handler_test",
    srcs = ["src/test/java/hibiki/ui/TimelineMouseHandlerTest.java"],
    test_class = "hibiki.ui.TimelineMouseHandlerTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "piano_roll_test",
    srcs = ["src/test/java/hibiki/ui/PianoRollTest.java"],
    test_class = "hibiki.ui.PianoRollTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "session_view_test",
    srcs = ["src/test/java/hibiki/ui/SessionViewTest.java"],
    test_class = "hibiki.ui.SessionViewTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "midi_data_model_test",
    srcs = ["src/test/java/hibiki/ui/MidiDataModelTest.java"],
    data = ["//testdata"],
    test_class = "hibiki.ui.MidiDataModelTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "plugin_pane_test",
    srcs = ["src/test/java/hibiki/ui/PluginPaneTest.java"],
    test_class = "hibiki.ui.PluginPaneTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "settings_dialog_test",
    srcs = ["src/test/java/hibiki/ui/SettingsDialogTest.java"],
    test_class = "hibiki.ui.SettingsDialogTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "session_view_ipc_test",
    srcs = ["src/test/java/hibiki/ui/SessionViewIpcTest.java"],
    test_class = "hibiki.ui.SessionViewIpcTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:junit_junit",
    ],
)

# Clojure test resources
java_library(
    name = "hibiki-clj-test-sources",
    resource_strip_prefix = "src/test/clojure",
    resources = glob(["src/test/clojure/**/*.clj"]),
)

# Clojure tests (via ClojureTestRunner)
java_test(
    name = "clojure_tests",
    srcs = ["src/test/java/hibiki/ClojureTestRunner.java"],
    data = ["//testdata"],
    main_class = "hibiki.ClojureTestRunner",
    test_class = "hibiki.ClojureTestRunner",
    use_testrunner = False,
    deps = [
        ":hibiki-clj-lib",
        ":hibiki-clj-sources",
        ":hibiki-clj-test-sources",
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
        "@clojure_core_specs_alpha_jar//jar",
        "@clojure_jar//jar",
        "@clojure_spec_alpha_jar//jar",
        "@maven//:com_google_protobuf_protobuf_java",
    ],
)
