package xyz.zhuzeitou.qrcode

enum class LogLevel(internal val nativeValue: Int) {
    VERBOSE(0),
    DEBUG(1),
    INFO(2),
    WARN(3),
    ERROR(4);

    companion object {
        internal fun fromNativeValue(value: Int): LogLevel? = when (value) {
            0 -> VERBOSE
            1 -> DEBUG
            2 -> INFO
            3 -> WARN
            4 -> ERROR
            else -> null
        }
    }
}
