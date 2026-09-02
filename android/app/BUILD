load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

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
    ],
)
