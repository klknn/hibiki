# Add project specific ProGuard rules here.
-keep class hibiki.pb.** { *; }
-keepclassmembers class hibiki.android.engine.HibikiEngine {
    native <methods>;
}
