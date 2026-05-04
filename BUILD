# Root BUILD
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
        "//engine:hbk-play",
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
        "//engine:hbk-play",
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
        "//engine:hbk-play",
        "//testdata",
    ],
    main_class = "hibiki.EchoMain",
    runtime_deps = [":hibiki-echo-lib"],
)

java_test(
    name = "backend_manager_test",
    srcs = ["src/test/java/hibiki/BackendManagerTest.java"],
    data = [
        "//engine:hbk-play",
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
        "//engine:hbk-play",
        "//engine:hbk-plugin-worker",
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
    name = "film_device_panel_test",
    srcs = ["src/test/java/hibiki/ui/FilmDevicePanelTest.java"],
    test_class = "hibiki.ui.FilmDevicePanelTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "envelope_editor_panel_test",
    srcs = ["src/test/java/hibiki/ui/EnvelopeEditorPanelTest.java"],
    test_class = "hibiki.ui.EnvelopeEditorPanelTest",
    deps = [
        ":hibiki-gui-lib",
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
        "//pb:core_java_proto",
        "//pb:notifications_java_proto",
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
    name = "timeline_notification_handler_test",
    srcs = ["src/test/java/hibiki/ui/TimelineNotificationHandlerTest.java"],
    test_class = "hibiki.ui.TimelineNotificationHandlerTest",
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
    name = "midi_data_model_test",
    srcs = [
        "src/test/java/hibiki/ui/MidiDataModelTest.java",
        "src/test/java/hibiki/ui/TestStateHelper.java",
    ],
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
    name = "scale_mode_test",
    srcs = ["src/test/java/hibiki/ui/ScaleModeTest.java"],
    test_class = "hibiki.ui.ScaleModeTest",
    deps = [
        ":hibiki-gui-lib",
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
    name = "plugin_pane_extended_test",
    srcs = ["src/test/java/hibiki/ui/PluginPaneExtendedTest.java"],
    test_class = "hibiki.ui.PluginPaneExtendedTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:notifications_java_proto",
        "@maven//:com_google_protobuf_protobuf_java",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "settings_dialog_test",
    srcs = ["src/test/java/hibiki/ui/SettingsDialogTest.java"],
    test_class = "hibiki.ui.SettingsDialogTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "topbar_test",
    srcs = ["src/test/java/hibiki/ui/TopBarTest.java"],
    test_class = "hibiki.ui.TopBarTest",
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

java_test(
    name = "absl_formatter_test",
    srcs = ["src/test/java/hibiki/AbslFormatterTest.java"],
    test_class = "hibiki.AbslFormatterTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "timeline_constants_test",
    srcs = ["src/test/java/hibiki/ui/TimelineConstantsTest.java"],
    test_class = "hibiki.ui.TimelineConstantsTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "marker_dialog_test",
    srcs = ["src/test/java/hibiki/ui/MarkerDialogTest.java"],
    test_class = "hibiki.ui.MarkerDialogTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "menu_bar_factory_test",
    srcs = ["src/test/java/hibiki/ui/MenuBarFactoryTest.java"],
    test_class = "hibiki.ui.MenuBarFactoryTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "automation_renderer_test",
    srcs = ["src/test/java/hibiki/ui/AutomationRendererTest.java"],
    test_class = "hibiki.ui.AutomationRendererTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "automation_editor_dialog_test",
    srcs = ["src/test/java/hibiki/ui/AutomationEditorDialogTest.java"],
    test_class = "hibiki.ui.AutomationEditorDialogTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "automation_editor_test",
    srcs = ["src/test/java/hibiki/ui/AutomationEditorTest.java"],
    test_class = "hibiki.ui.AutomationEditorTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "simple_laf_test",
    srcs = ["src/test/java/hibiki/SimpleLafTest.java"],
    test_class = "hibiki.SimpleLafTest",
    deps = [
        ":hibiki-gui-lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "audio_editor_panel_test",
    srcs = ["src/test/java/hibiki/ui/AudioEditorPanelTest.java"],
    test_class = "hibiki.ui.AudioEditorPanelTest",
    deps = [
        ":hibiki-gui-lib",
        "//pb:commands_java_proto",
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
