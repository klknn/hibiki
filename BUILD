load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test", "objc_library")
load("@rules_java//java:defs.bzl", "java_binary", "java_library", "java_test")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:java_proto_library.bzl", "java_proto_library")

cc_library(
    name = "alsa_out",
    srcs = ["alsa_out.cpp"],
    hdrs = ["alsa_out.hpp"],
    target_compatible_with = ["@platforms//os:linux"],
    linkopts = ["-lasound"],
)

cc_library(
    name = "win32_out",
    srcs = ["win32_out.cpp"],
    hdrs = ["win32_out.hpp"],
    target_compatible_with = ["@platforms//os:windows"],
    linkopts = ["-DEFAULTLIB:ole32"],
)

cc_library(
    name = "vst3_host",
    srcs = ["vst3_host.cpp"],
    hdrs = ["vst3_host.hpp", "vst3_host_impl.hpp"],
    deps = [
        "@vst3sdk//:vst3sdk",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": [
            "-lpthread",
            "-ldl",
        ],
    }),
)

cc_library(
    name = "vst3_host_x11",
    srcs = ["vst3_host_x11.cpp"],
    target_compatible_with = ["@platforms//os:linux"],
    deps = [
        ":vst3_host",
        "@vst3sdk//:vst3sdk",
    ],
    linkopts = ["-lX11", "-lXcursor"],
    alwayslink = True,
)

cc_library(
    name = "vst3_host_win32",
    srcs = ["vst3_host_win32.cpp"],
    target_compatible_with = ["@platforms//os:windows"],
    deps = [
        ":vst3_host",
        "@vst3sdk//:vst3sdk",
    ],
    linkopts = ["-DEFAULTLIB:user32"],
    alwayslink = True,
)

cc_library(
    name = "coreaudio_out",
    srcs = ["coreaudio_out.cpp"],
    hdrs = ["coreaudio_out.hpp"],
    target_compatible_with = ["@platforms//os:macos"],
    linkopts = [
        "-framework CoreAudio",
        "-framework AudioUnit",
        "-framework AudioToolbox",
    ],
)

objc_library(
    name = "vst3_host_mac",
    srcs = ["vst3_host_mac.mm"],
    target_compatible_with = ["@platforms//os:macos"],
    deps = [
        ":vst3_host",
        "@vst3sdk//:vst3sdk",
    ],
    alwayslink = True,
)

cc_library(
    name = "midi",
    srcs = ["midi.cpp"],
    hdrs = ["midi.hpp"],
)

cc_library(
    name = "track",
    srcs = ["track.cpp"],
    hdrs = ["track.hpp"],
    deps = [
        ":ipc",
        ":clip",
        ":vst3_host",
    ],
)

cc_library(
    name = "project",
    srcs = ["project.cpp"],
    hdrs = ["project.hpp"],
    deps = [
        ":track",
        ":ipc",
        ":hibiki_cc_proto",
        ":audio_file",
    ],
)

cc_library(
    name = "ipc",
    srcs = ["ipc.cpp"],
    hdrs = ["ipc.hpp"],
    deps = [
        ":vst3_host",
        ":hibiki_cc_proto",
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

cc_binary(
    name = "hbk-play",
    srcs = [
        "main.cpp",
    ],
    deps = [
        ":audio_file",
        ":clip",
        ":history",
        ":ipc",
        ":midi",
        ":project",
        ":track",
    ] + select({
        "@platforms//os:windows": [
            ":win32_out",
            ":vst3_host_win32",
        ],
        "@platforms//os:macos": [
            ":coreaudio_out",
            ":vst3_host_mac",
        ],
        "//conditions:default": [
            ":alsa_out",
            ":vst3_host_x11",
        ],
    }),
    linkstatic = True,
)

cc_library(
    name = "test_utils",
    hdrs = ["test_utils.hpp"],
    testonly = True,
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
    deps = [
        ":audio_file",
        ":test_utils",
        "@googletest//:gtest_main",
    ],
    linkstatic = True,
)

cc_library(
    name = "clip",
    srcs = ["clip.cpp"],
    hdrs = ["clip.hpp"],
    deps = [
        ":audio_file",
        ":midi",
    ],
)

cc_test(
    name = "clip_test",
    srcs = ["clip_test.cpp"],
    data = ["//testdata"],
    deps = [
        ":clip",
        ":test_utils",
        "@googletest//:gtest_main",
    ],
    linkstatic = True,
)

cc_test(
    name = "track_test",
    srcs = ["track_test.cpp"],
    data = ["//testdata"],
    deps = [
        ":track",
        ":test_utils",
        "@googletest//:gtest_main",
    ],
    linkstatic = True,
)

cc_test(
    name = "project_test",
    size = "small",
    srcs = ["project_test.cpp"],
    data = ["//testdata"],
    deps = [
        ":project",
        ":test_utils",
        ":audio_file",
        "@googletest//:gtest_main",
    ],
    linkstatic = True,
)

proto_library(
    name = "hibiki_proto",
    srcs = ["hibiki.proto"],
)

cc_proto_library(
    name = "hibiki_cc_proto",
    deps = [":hibiki_proto"],
)

java_proto_library(
    name = "hibiki_java_proto",
    deps = [":hibiki_proto"],
)








java_library(
    name = "hibiki-gui-lib",
    srcs = glob(["src/main/java/hibiki/**/*.java"], exclude = ["src/main/java/hibiki/ClojureMain.java", "src/main/java/hibiki/EchoMain.java"]),
    resources = glob(["src/main/resources/**/*"]),
    deps = [
        ":hibiki_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:com_formdev_flatlaf",
    ],
    visibility = ["//visibility:public"],
)

java_binary(
    name = "hibiki-gui-java",
    main_class = "hibiki.GuiMain",
    runtime_deps = [":hibiki-gui-lib"],
    data = [":hbk-play", "//testdata"],
)

# Clojure source files — strip src/main/clojure/ so Clojure RT can find them
java_library(
    name = "hibiki-clj-sources",
    resources = glob(["src/main/clojure/**/*.clj"]),
    resource_strip_prefix = "src/main/clojure",
)

# Clojure frontend — shares Java backend classes, adds Clojure sources as resources
java_library(
    name = "hibiki-clj-lib",
    srcs = ["src/main/java/hibiki/ClojureMain.java"],
    deps = [
        ":hibiki-gui-lib",
        ":hibiki-clj-sources",
        "@clojure_jar//jar",
        "@clojure_spec_alpha_jar//jar",
        "@clojure_core_specs_alpha_jar//jar",
    ],
)

java_binary(
    name = "hibiki-gui-clj",
    main_class = "hibiki.ClojureMain",
    runtime_deps = [":hibiki-clj-lib"],
    data = [":hbk-play", "//testdata"],
)

# Echo hybrid frontend — Java components + Clojure glue
java_library(
    name = "hibiki-echo-lib",
    srcs = ["src/main/java/hibiki/EchoMain.java"],
    deps = [
        ":hibiki-gui-lib",
        ":hibiki-clj-sources",
        "@clojure_jar//jar",
        "@clojure_spec_alpha_jar//jar",
        "@clojure_core_specs_alpha_jar//jar",
    ],
)

java_binary(
    name = "hibiki-gui-echo",
    main_class = "hibiki.EchoMain",
    runtime_deps = [":hibiki-echo-lib"],
    data = [":hbk-play", "//testdata"],
)

java_test(
    name = "backend_manager_test",
    srcs = ["src/test/java/hibiki/BackendManagerTest.java"],
    test_class = "hibiki.BackendManagerTest",
    deps = [
        ":hibiki-gui-lib",
        ":hibiki_java_proto",
        "@maven//:junit_junit",
        "@maven//:com_google_protobuf_protobuf_java",
    ],
    data = [
        ":hbk-play",
        "//testdata",
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
        ":hibiki_java_proto",
        "@maven//:junit_junit",
        "@maven//:com_google_protobuf_protobuf_java",
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
    test_class = "hibiki.ui.MidiDataModelTest",
    data = ["//testdata"],
    deps = [
        ":hibiki-gui-lib",
        ":hibiki_java_proto",
        "@maven//:junit_junit",
        "@maven//:com_google_protobuf_protobuf_java",
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
        ":hibiki_java_proto",
        "@maven//:junit_junit",
        "@maven//:com_google_protobuf_protobuf_java",
    ],
)

# Clojure test resources
java_library(
    name = "hibiki-clj-test-sources",
    resources = glob(["src/test/clojure/**/*.clj"]),
    resource_strip_prefix = "src/test/clojure",
)

# Clojure tests (via ClojureTestRunner)
java_test(
    name = "clojure_tests",
    srcs = ["src/test/java/hibiki/ClojureTestRunner.java"],
    main_class = "hibiki.ClojureTestRunner",
    test_class = "hibiki.ClojureTestRunner",
    use_testrunner = False,
    deps = [
        ":hibiki-clj-lib",
        ":hibiki-clj-sources",
        ":hibiki-clj-test-sources",
        ":hibiki-gui-lib",
        ":hibiki_java_proto",
        "@clojure_jar//jar",
        "@clojure_spec_alpha_jar//jar",
        "@clojure_core_specs_alpha_jar//jar",
        "@maven//:com_google_protobuf_protobuf_java",
    ],
    data = ["//testdata"],
)
