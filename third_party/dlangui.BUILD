load("@hibiki//tools/d_rules:defs.bzl", "d_library")

d_library(
    name = "dlangui",
    srcs = glob(
        [
            "src/**/*.d",
            "3rdparty/**/*.d",
        ],
        exclude = [
            "src/**/*_test.d",
            "3rdparty/android/**",
            "3rdparty/EGL/**",
            "3rdparty/GLES2/**",
            "3rdparty/win32/**",
            "src/dlangui/platforms/windows/**",
            "src/dlangui/platforms/x11/**",
            "src/dlangui/platforms/android/**",
            "src/dlangui/platforms/console/**",
        ],
    ),
    d_versions = [
        "USE_SDL",
        "USE_OPENGL",
        "USE_FREETYPE",
        "EmbedStandardResources",
    ],
    includes = [
        "src",
        "3rdparty",
    ],
    string_import_dirs = ["views"],
    string_imports = glob(["views/**/*"]),
    visibility = ["//visibility:public"],
    deps = [
        "@arsd_official//:arsd",
        "@bindbc_freetype//:bindbc_freetype",
        "@bindbc_loader//:bindbc_loader",
        "@bindbc_opengl//:bindbc_opengl",
        "@bindbc_sdl//:bindbc_sdl",
        "@icontheme//:icontheme",
        "@inilike//:inilike",
    ],
)
