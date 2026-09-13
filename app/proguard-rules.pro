# The JNI entry points are looked up by name from C++, so they must survive
# shrinking even though nothing in Kotlin calls them reflectively.
-keepclasseswithmembernames class * {
    native <methods>;
}
-keep class com.dylan.harmonizer.NativeBridge { *; }
