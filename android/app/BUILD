load("@rules_android//rules:rules.bzl", "android_binary", "android_library", "android_local_test")
load("@rules_java//java:defs.bzl", "java_library", "java_test")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

package(default_visibility = ["//visibility:public"])

android_library(
    name = "hibiki_app_lib",
    srcs = glob(["src/main/java/**/*.java"]),
    custom_package = "hibiki.android",
    manifest = "src/main/AndroidManifest.xml",
)

android_binary(
    name = "app",
    custom_package = "hibiki.android",
    debug_key = "debug.keystore",
    manifest = "src/main/AndroidManifest.xml",
    deps = [":hibiki_app_lib"],
)

# Fast JVM model unit test target (runs instantly without Android emulator)
# bazel test //android/app:model_test -c opt --test_output=all
java_library(
    name = "model_lib",
    srcs = glob(["src/main/java/hibiki/android/model/**/*.java"]),
)

java_test(
    name = "model_test",
    srcs = ["src/test/java/hibiki/android/model/ModelTest.java"],
    test_class = "hibiki.android.model.ModelTest",
    deps = [
        ":model_lib",
        "@maven//:junit_junit",
    ],
)

java_test(
    name = "step_sequencer_test",
    srcs = [
        "src/test/java/hibiki/android/model/StepSequencerTest.java",
    ],
    test_class = "hibiki.android.model.StepSequencerTest",
    deps = [
        ":model_lib",
        "@maven//:junit_junit",
    ],
)

android_local_test(
    name = "hibiki_engine_playback_test",
    srcs = ["src/test/java/hibiki/android/engine/HibikiEnginePlaybackTest.java"],
    custom_package = "hibiki.android",
    manifest = "src/main/AndroidManifest.xml",
    test_class = "hibiki.android.engine.HibikiEnginePlaybackTest",
    deps = [
        ":hibiki_app_lib",
        "@maven//:junit_junit",
    ],
)

# Build target: bazel run //android/app:build
sh_binary(
    name = "build",
    srcs = ["//tools:build_android_apk.sh"],
    data = [
        ":app",
    ],
)

# Run target: bazel run //android/app:run
sh_binary(
    name = "run",
    srcs = ["//tools:run_android_app.sh"],
    data = [
        ":build",
        "//engine/android:libhibiki_jni.so",
        "//tools:start_android_emulator.sh",
    ],
)

# End-to-end (E2E) instrumented test running on simulator:
# bazel test //android/app:e2e_test -c opt --test_output=all
sh_test(
    name = "e2e_test",
    size = "large",
    srcs = ["//tools:test_android_e2e.sh"],
    data = [
        ":build",
        "//android:gradle_files",
        "//engine/android:libhibiki_jni.so",
        "//tools:start_android_emulator.sh",
    ] + glob([
        "src/main/java/**/*.java",
        "src/main/AndroidManifest.xml",
        "src/androidTest/java/**/*.java",
    ]),
    local = True,
    tags = [
        "exclusive",
        "local",
        "manual",
    ],
)
