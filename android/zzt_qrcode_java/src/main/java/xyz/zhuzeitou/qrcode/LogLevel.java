package xyz.zhuzeitou.qrcode;

/** Log severity emitted by the native QR code library. */
public enum LogLevel {
    VERBOSE(0),
    DEBUG(1),
    INFO(2),
    WARN(3),
    ERROR(4);

    private final int nativeValue;

    LogLevel(int nativeValue) {
        this.nativeValue = nativeValue;
    }

    int nativeValue() {
        return nativeValue;
    }

    static LogLevel fromNativeValue(int value) {
        switch (value) {
            case 0:
                return VERBOSE;
            case 1:
                return DEBUG;
            case 2:
                return INFO;
            case 3:
                return WARN;
            case 4:
                return ERROR;
            default:
                return null;
        }
    }
}
