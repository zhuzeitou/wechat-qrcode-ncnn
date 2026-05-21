# JNI_OnLoad looks up this exact class name. RegisterNatives binds native
# methods by their original names, and native code calls dispatchLog by name.
-keep class xyz.zhuzeitou.qrcode.NativeLib {
    native <methods>;
    static void dispatchLog(int, java.lang.String);
}
