# JNI_OnLoad looks up this exact class name. RegisterNatives binds external
# methods by their original JVM names, and native code calls dispatchLog by name.
-keep class xyz.zhuzeitou.qrcode.NativeLib {
    native <methods>;
    static void dispatchLog(int, java.lang.String);
}
