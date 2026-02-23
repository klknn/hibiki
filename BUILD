load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_java//java:defs.bzl", "java_binary", "java_library", "java_test")
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
    srcs = [
        "main.cpp",
        "midi.cpp",
        "midi.hpp",
    ],
    deps = [
        ":alsa_out",
        ":vst3_host",
        ":vst3_host_x11",
        ":hibiki_request_cc",
        ":hibiki_response_cc",
        ":hibiki_project_cc",
    ],
    linkstatic = True,
)

cc_test(
    name = "midi_test",
    srcs = ["midi_test.cpp", "midi.hpp", "midi.cpp"],
    data = ["//testdata"],
)

flatbuffer_cc_library(
    name = "hibiki_request_cc",
    srcs = ["hibiki_request.fbs"],
)

flatbuffer_cc_library(
    name = "hibiki_response_cc",
    srcs = ["hibiki_response.fbs"],
)

flatbuffer_cc_library(
    name = "hibiki_project_cc",
    srcs = ["hibiki_project.fbs"],
)

flatbuffer_library_public(
    name = "hibiki_request_java_gen",
    srcs = ["hibiki_request.fbs"],
    outs = [
        "hibiki/ipc/Command.java",
        "hibiki/ipc/CommandUnion.java",
        "hibiki/ipc/LoadClip.java",
        "hibiki/ipc/LoadClipT.java",
        "hibiki/ipc/LoadPlugin.java",
        "hibiki/ipc/LoadPluginT.java",
        "hibiki/ipc/Play.java",
        "hibiki/ipc/PlayT.java",
        "hibiki/ipc/PlayClip.java",
        "hibiki/ipc/PlayClipT.java",
        "hibiki/ipc/Quit.java",
        "hibiki/ipc/QuitT.java",
        "hibiki/ipc/RemovePlugin.java",
        "hibiki/ipc/RemovePluginT.java",
        "hibiki/ipc/Request.java",
        "hibiki/ipc/RequestT.java",
        "hibiki/ipc/SetParamValue.java",
        "hibiki/ipc/SetParamValueT.java",
        "hibiki/ipc/ShowPluginGui.java",
        "hibiki/ipc/ShowPluginGuiT.java",
        "hibiki/ipc/SetClipLoop.java",
        "hibiki/ipc/SetClipLoopT.java",
        "hibiki/ipc/Stop.java",
        "hibiki/ipc/StopT.java",
        "hibiki/ipc/StopTrack.java",
        "hibiki/ipc/StopTrackT.java",
        "hibiki/ipc/SaveProject.java",
        "hibiki/ipc/SaveProjectT.java",
        "hibiki/ipc/LoadProject.java",
        "hibiki/ipc/LoadProjectT.java",
    ],
    language_flag = "--java --gen-object-api",
)

flatbuffer_library_public(
    name = "hibiki_response_java_gen",
    srcs = ["hibiki_response.fbs"],
    outs = [
        "hibiki/ipc/Notification.java",
        "hibiki/ipc/NotificationT.java",
        "hibiki/ipc/ParamInfo.java",
        "hibiki/ipc/ParamInfoT.java",
        "hibiki/ipc/ParamList.java",
        "hibiki/ipc/ParamListT.java",
        "hibiki/ipc/Log.java",
        "hibiki/ipc/LogT.java",
        "hibiki/ipc/Acknowledge.java",
        "hibiki/ipc/AcknowledgeT.java",
        "hibiki/ipc/ClipInfo.java",
        "hibiki/ipc/ClipInfoT.java",
        "hibiki/ipc/ClearProject.java",
        "hibiki/ipc/ClearProjectT.java",
        "hibiki/ipc/TrackLevel.java",
        "hibiki/ipc/TrackLevelT.java",
        "hibiki/ipc/TrackLevels.java",
        "hibiki/ipc/TrackLevelsT.java",
        "hibiki/ipc/ClipWaveform.java",
        "hibiki/ipc/ClipWaveformT.java",
        "hibiki/ipc/Response.java",
        "hibiki/ipc/ResponseUnion.java",
    ],
    language_flag = "--java --gen-object-api",
)

flatbuffer_library_public(
    name = "hibiki_project_java_gen",
    srcs = ["hibiki_project.fbs"],
    outs = [
        "hibiki/project/Clip.java",
        "hibiki/project/ClipT.java",
        "hibiki/project/Parameter.java",
        "hibiki/project/ParameterT.java",
        "hibiki/project/Plugin.java",
        "hibiki/project/PluginT.java",
        "hibiki/project/Project.java",
        "hibiki/project/ProjectT.java",
        "hibiki/project/Track.java",
        "hibiki/project/TrackT.java",
    ],
    language_flag = "--java --gen-object-api",
)

java_library(
    name = "hibiki_request_java_lib",
    srcs = [":hibiki_request_java_gen"],
    deps = ["@maven//:com_google_flatbuffers_flatbuffers_java"],
)

java_library(
    name = "hibiki_response_java_lib",
    srcs = [":hibiki_response_java_gen"],
    deps = ["@maven//:com_google_flatbuffers_flatbuffers_java"],
)

java_library(
    name = "hibiki_project_java_lib",
    srcs = [":hibiki_project_java_gen"],
    deps = ["@maven//:com_google_flatbuffers_flatbuffers_java"],
)

java_library(
    name = "hibiki-gui-lib",
    srcs = glob(["src/main/java/hibiki/**/*.java"]),
    deps = [
        ":hibiki_request_java_lib",
        ":hibiki_response_java_lib",
        ":hibiki_project_java_lib",
        "@maven//:com_google_flatbuffers_flatbuffers_java",
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

java_test(
    name = "backend_manager_test",
    srcs = ["src/test/java/hibiki/BackendManagerTest.java"],
    test_class = "hibiki.BackendManagerTest",
    deps = [
        ":hibiki-gui-lib",
        ":hibiki_request_java_lib",
        ":hibiki_response_java_lib",
        "@maven//:junit_junit",
        "@maven//:com_google_flatbuffers_flatbuffers_java",
    ],
    data = [
        ":hbk-play",
        "//testdata",
    ],
)
