load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

package(default_visibility = ["//visibility:public"])

# Build target: bazel run //android/app:build
sh_binary(
    name = "build",
    srcs = ["//tools:build_android_apk.sh"],
    data = [
        "build.gradle",
        "proguard-rules.pro",
        "//android:gradle_files",
        "//engine/android:libhibiki_jni.so",
    ] + glob([
        "src/main/java/**/*.java",
        "src/main/AndroidManifest.xml",
    ]),
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
