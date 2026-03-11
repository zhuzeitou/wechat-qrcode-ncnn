# WeChat QRCode NCNN

![C++](https://img.shields.io/badge/language-C++-blue.svg)
![macOS](https://img.shields.io/badge/platform-macOS-lightgrey.svg)
![Android](https://img.shields.io/badge/platform-Android-green.svg)
![Unity](https://img.shields.io/badge/platform-Unity-black.svg)
![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)

A high-performance WeChat QRCode recognition library based on [ncnn](https://github.com/Tencent/ncnn). This project is ported from [OpenCV WeChat QRCode](https://github.com/opencv/opencv_contrib/tree/master/modules/wechat_qrcode), but completely removes the OpenCV dependency by using **ncnn** for inference and **ncnn**'s **simpleocv** module for image processing. Additionally, the result point coordinates have been reordered to be consistent with [ZXing](https://github.com/zxing/zxing): 3 points (bottom-left, top-left, top-right) for Version 1, or 4 points (bottom-left, top-left, top-right, alignment) for higher versions.

This makes the library significantly more lightweight and easier to integrate into various platforms, especially for mobile and game development.

## Features

*   **High Performance**: Powered by ncnn, optimized for mobile platforms.
*   **Lightweight**: No heavy OpenCV dependencies. The entire image processing pipeline is implemented using ncnn's `simpleocv`.
*   **Self-Contained**: All neural network models (SSD detector + super-resolution) are pre-converted to ncnn format and embedded as C arrays at compile time. No external model files needed at runtime.
*   **Easy Integration**: Pure C API (`extern "C"`) for seamless FFI integration across languages.
*   **Thread-Safe**: Handle-based architecture with read-write locks ensures safe concurrent access.
*   **Robust**: Inherits the excellent detection capabilities of WeChat QRCode for complex scenarios (blurred, small, non-standard QRCodes).
*   **Cross-Platform**: Supports Windows, macOS, Linux, Android, and Unity (via C/C++ API).

## Prerequisites

To build this project, you need:

*   **CMake** (3.20 or higher)
*   **Ninja** (Recommended build tool)
*   **C++ Compiler** (supporting C++17)
*   **Android NDK** (for Android build)
*   **Visual Studio** or **MinGW** (for Windows build)

## Build Instructions

> **Note**: This repository does **not** provide pre-compiled binaries. You need to build the libraries from source for your target platform.
> We strongly recommend using `cmake --install <build_dir> --prefix <output_dir>` to extract the compiled artifacts to a clean directory.

### Quick Start (Standard Build)

This is the standard native build method. It works out-of-the-box for **Linux**, **macOS** (building for your current host architecture), and **Windows** (if running inside a VS Developer Command Prompt or MSYS2 environment).

```bash
# Clone the repository with submodules
git clone --recursive https://github.com/zhuzeitou/wechat-qrcode-ncnn.git
cd wechat-qrcode-ncnn

# standard cmake build (uses default generator, e.g., Make, Ninja, or MSBuild)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4

# install artifacts (headers and library) to ./install_output
cmake --install build --prefix ./install_output
```

### Windows

**Option 1: Visual Studio Solution**

1.  Generate the solution:
    ```cmd
    cmake -B build -A x64 -DBUILD_TESTS=ON
    ```
2.  Build via command line (or open `.sln` in VS):
    ```cmd
    cmake --build build --config Release -j 4
    ```
3.  Install artifacts:
    ```cmd
    cmake --install build --prefix ./install_output
    ```

**Option 2: Ninja (Fast)**

1.  Open **x64 Native Tools Command Prompt for VS 20xx**.
2.  Run the following commands:
    ```cmd
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
    cmake --build build -j 4
    cmake --install build --prefix ./install_output
    ```

**Option 3: MSYS2 (MinGW)**

1.  Open **MSYS2 UCRT64** (or CLANG64) terminal.
2.  Install dependencies:
    ```bash
    pacman -S mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-toolchain
    ```
3.  Run the following commands:
    ```bash
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
    cmake --build build -j 4
    cmake --install build --prefix ./install_output
    ```

> **Note**:
> After installation, you can find the generated library `zzt_qrcode.dll` in `install_output/bin/` and headers in `install_output/include/`. On Windows, `.dll` files are installed to the `bin/` directory alongside executables.

### macOS

For macOS, you should build the `x86_64` and `arm64` architectures separately and then merge them. 

**1. Use the iOS Toolchain for Cross-Compilation**

When building for an architecture different from your host (e.g., building `x86_64` on Apple Silicon), relying solely on `-DCMAKE_OSX_ARCHITECTURES` is insufficient as CMake may still use the host's processor for configuration, leading to incorrect instruction set detection.
Instead, it is **highly recommended** to explicitly use the `ios.toolchain.cmake` provided by the `ncnn` submodule (originally maintained by [leetal/ios-cmake](https://github.com/leetal/ios-cmake)). This toolchain correctly configures the environment under the hood to ensure `ncnn` enables the proper optimizations for the target architecture.

**2. Avoid Universal Binaries in CMake**

Do **not** attempt to compile a Universal Fat Library in a single CMake pass (e.g., using `-DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"` or the toolchain's `MAC_UNIVERSAL` platform). Due to `ncnn`'s reliance on CPU instruction set detection (like ARM NEON and Intel AVX) during the configuration stage, a multi-architecture target will cause these detection macros to fail, resulting in the loss of all hardware-accelerated optimizations.

Configure, build, and install each architecture separately using the toolchain, then merge them using `lipo`:

```bash
# 1. Build and install for x86_64
cmake -B build-mac-x86_64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=core/third_party/ncnn/toolchains/ios.toolchain.cmake \
    -DPLATFORM=MAC
cmake --build build-mac-x86_64 -j 4
cmake --install build-mac-x86_64 --prefix ./install_mac/x86_64

# 2. Build and install for arm64 (Apple Silicon)
cmake -B build-mac-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=core/third_party/ncnn/toolchains/ios.toolchain.cmake \
    -DPLATFORM=MAC_ARM64
cmake --build build-mac-arm64 -j 4
cmake --install build-mac-arm64 --prefix ./install_mac/arm64

# 3. Create a Universal Fat Library
mkdir -p ./install_mac/universal/lib
lipo -create \
    ./install_mac/x86_64/lib/libzzt_qrcode.dylib \
    ./install_mac/arm64/lib/libzzt_qrcode.dylib \
    -output ./install_mac/universal/lib/libzzt_qrcode.dylib
```

### Android

**Option 1: Android Studio (Recommended)**

The Android integration consists of two parts:

*   **`android-jni/`** (project root): Android-specific JNI C wrapper that bridges the core library to the Java/Kotlin layer.
*   **`android/`**: Gradle project containing two library modules:
    *   **`zzt_qrcode_kotlin`**: (Recommended) Kotlin implementation with coroutine support.
    *   **`zzt_qrcode_java`**: Java implementation.

1.  Open the `android/` directory in **Android Studio**.
2.  Sync Gradle.
3.  Build the desired module (e.g., `:zzt_qrcode_kotlin:assembleRelease` or `:zzt_qrcode_java:assembleRelease`).

> **Note on 16k Page Size**:
> Android 15 introduces support for 16KB page sizes. To ensure compatibility, `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` is enabled in `build.gradle.kts` and CMake arguments.
> If you are building manually via command line, you should add `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON`.

**Option 2: CMake Command Line**

You can also build the `.so` files manually using the NDK toolchain file.

```bash
# Set your NDK path
export ANDROID_NDK=/path/to/android-ndk

# Build and Install for arm64-v8a
cmake -B build-android-arm64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI="arm64-v8a" \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ANDROID_JNI=ON
cmake --build build-android-arm64 -j 4
cmake --install build-android-arm64 --prefix ./install_android/arm64-v8a

# Build and Install for armeabi-v7a
cmake -B build-android-armv7 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI="armeabi-v7a" \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_ANDROID_JNI=ON
cmake --build build-android-armv7 -j 4
cmake --install build-android-armv7 --prefix ./install_android/armeabi-v7a
```

> After the installation is complete, you can find the isolated artifacts for each architecture in `install_android/<abi>/lib/` (e.g., `libzzt_qrcode.so` and `libzzt_qrcode_jni.so`).

### Unity

To use this library in Unity, you need to compile the native plugins (`.dll` for Windows / `.so` for Android) yourself and place them into the Unity project.

1.  **Build for Windows (x64)**:
    *   Build `zzt_qrcode.dll` and run `--install` using the instructions in the [Windows](#windows) section above.
    *   Copy the generated `install_output/bin/zzt_qrcode.dll` to `unity/xyz.zhuzeitou.qrcode/Plugins/Windows/x86_64/`.

2.  **Build for macOS (Universal)**:
    *   Build the universal `libzzt_qrcode.dylib` using the `lipo` instructions in the [macOS](#macos) section above.
    *   Copy the generated `install_mac/universal/lib/libzzt_qrcode.dylib` to `unity/xyz.zhuzeitou.qrcode/Plugins/macOS/`.

3.  **Build for Android (Multi-Architecture)**:
    *   Build and install both `arm64-v8a` and `armeabi-v7a` using the CMake commands in the [Android](#android) section above.
    *   Copy the generated `libzzt_qrcode.so` files from their respective `install_android/<abi>/lib/` folders to the corresponding Unity plugin folders:
        *   `arm64-v8a` -> `unity/xyz.zhuzeitou.qrcode/Plugins/Android/libs/arm64-v8a/`
        *   `armeabi-v7a` -> `unity/xyz.zhuzeitou.qrcode/Plugins/Android/libs/armeabi-v7a/`

## Usage

### C/C++

```cpp
#include "zzt_qrcode/qrcode.h"

// Initialize detector
zzt_qrcode_detector_h detector = zzt_qrcode_create_detector();

// Detect from file
zzt_qrcode_result_h result = nullptr;
zzt_qrcode_error_t err = zzt_qrcode_detect_and_decode_path_u8(detector, "path/to/qrcode.jpg", &result);
if (err != ZZT_QRCODE_OK) {
    printf("Detection failed: %d\n", err);
    zzt_qrcode_release_detector(detector);
    return;
}

// Get results
int size = 0;
zzt_qrcode_get_result_size(result, &size);
for (int i = 0; i < size; ++i) {
    // Get decoded text
    int len;
    zzt_qrcode_get_result_text(result, i, nullptr, &len);
    char text[len] = {0};
    zzt_qrcode_get_result_text(result, i, text, &len);
    printf("QRCode[%d]: %s\n", i, text);

    // Get vertex coordinates (ZXing-consistent order: bottom-left, top-left, top-right, [alignment])
    // 3 points (6 floats) for Version 1, or 4 points (8 floats) for higher versions
    int point_count;
    zzt_qrcode_get_result_points(result, i, nullptr, &point_count);
    float points[point_count] = {0};
    zzt_qrcode_get_result_points(result, i, points, &point_count);
    for (int j = 0; j < point_count / 2; ++j) {
        printf("  Point[%d]: (%.1f, %.1f)\n", j, points[j*2], points[j*2+1]);
    }
}

// Cleanup
zzt_qrcode_release_result(result);
zzt_qrcode_release_detector(detector);
```

### Android

**Kotlin** (Recommended — with Coroutine support)

```kotlin
import xyz.zhuzeitou.qrcode.QrcodeDetector
import xyz.zhuzeitou.qrcode.QrcodeResults
import xyz.zhuzeitou.qrcode.QrcodeResults.QrcodeErrorCode

// Coroutine-based detection with structured concurrency
lifecycleScope.launch {
    QrcodeDetector().use { detector ->
        // Detect from Bitmap (suspend function)
        val results = detector.detectQRCode(bitmap)

        // Or detect from file path
        // val results = detector.detectQRCode("/path/to/image.jpg")

        if (results.errorCode == QrcodeErrorCode.OK) {
            results.qrcodeResults?.forEach { result ->
                Log.d("QRCode", "Text: ${result.text}")
                result.resultPoints.forEach { point ->
                    Log.d("QRCode", "Point: (${point.x}, ${point.y})")
                }
            }
        }
    }
}

// Synchronous and callback-based APIs are also available
// detector.detectQRCodeSync(bitmap)
// detector.detectQRCode(bitmap) { results -> ... }
```

**Java**

```java
import xyz.zhuzeitou.qrcode.QrcodeDetector;
import xyz.zhuzeitou.qrcode.QrcodeResults;

// Initialize detector (using try-with-resources to automatically close/release)
try (QrcodeDetector detector = new QrcodeDetector()) {
    
    // Detect from Bitmap
    QrcodeResults results = detector.detectQRCodeSync(bitmap);
    
    // Or detect from file path
    // QrcodeResults results = detector.detectQRCodeSync("/path/to/image.jpg");

    if (results.errorCode == QrcodeResults.QrcodeErrorCode.OK) {
        for (QrcodeResults.QrcodeResult result : results.qrcodeResults) {
            Log.d("QRCode", "Text: " + result.text);
            if (result.resultPoints != null) {
                 for (QrcodeResults.ResultPoint point : result.resultPoints) {
                     Log.d("QRCode", "Point: (" + point.x + ", " + point.y + ")");
                 }
            }
        }
    }
} catch (Exception e) {
    e.printStackTrace();
}

// Async detection is also supported
// detector.detectQRCode(bitmap, results -> { ... });
```

### Unity (C#)

```csharp
using ZZT.QRCode;

// Initialize detector
// QrcodeDetector implements IDisposable, so use 'using' statement to ensure resource release
using (var detector = new QrcodeDetector())
{
    // Async detection
    var results = await detector.DetectAndDecode(texture);

    if (results.Error == QrcodeResults.ErrorCode.OK)
    {
        foreach (var result in results.Results)
        {
            Debug.Log($"Text: {result.Text}");
        }
    }
}
```

### NCNN Dependency Configuration

By default, this project uses a Git submodule to integrate `ncnn` and links it statically. This ensures a self-contained library without external dependencies.

*   **Default Behavior**: STATIC linking, git submodule in `core/third_party/ncnn`.
*   **Required Options**: The internal ncnn is compiled with `-DNCNN_SIMPLEOCV=ON` to provide `cv::Mat` implementations without OpenCV.

If you wish to use a system-wide `ncnn` or a shared library:

1.  Modify `core/CMakeLists.txt` to enable `find_package(ncnn)` or change the linking logic.
2.  **Crucial**: Your external `ncnn` **MUST** be compiled with `-DNCNN_SIMPLEOCV=ON`. Otherwise, you will encounter missing symbol errors (e.g., `cv::Mat`).

## Architecture

```
┌──────────────────────────────────────────────────┐
│                    Application                   │
├────────────────┬────────────────┬────────────────┤
│    Android     │     Unity      │  Direct C/C++  │
│    (Kotlin     │    (C# with    │  Integration   │
│    / Java)     │    P/Invoke)   │                │
├────────────────┴────────────────┴────────────────┤
│              Core C API (qrcode.h)               │
│        zzt_qrcode.dll / libzzt_qrcode.so         │
├──────────────────────────────────────────────────┤
│  WeChat QRCode Engine (SSD Detector + Zxing)     │
│  ncnn (inference) + simpleocv (image processing) │
│  Models embedded as C arrays (no external files) │
└──────────────────────────────────────────────────┘
```

The project is organized into the following directories:

*   **`core/`**: C++ core library with the public C API, WeChat QRCode engine, and ncnn as a Git submodule.
*   **`android-jni/`**: Android-specific JNI C wrapper that bridges the core library to Java/Kotlin.
*   **`android/`**: Android Gradle project with platform wrappers (`zzt_qrcode_kotlin`, `zzt_qrcode_java`).
*   **`tests/`**: Test programs for the core library.
*   **`unity/`**: Unity Package with C# wrappers and native plugin folders.

## Roadmap

*   [x] **Windows**: Tested and supported.
*   [x] **Linux**: Tested and supported.
*   [x] **Android**: Tested and supported.
*   [x] **macOS**: Tested and supported.
*   [ ] **iOS**: Planned.

## Acknowledgements

This project stands on the shoulders of giants:

*   **[Tencent/ncnn](https://github.com/Tencent/ncnn)**: For the high-performance neural network inference framework.
*   **[OpenCV WeChat QRCode](https://github.com/opencv/opencv_contrib/tree/master/modules/wechat_qrcode)**: For the original WeChat QRCode algorithm and models.
*   **[EdVince/QRCode-NCNN](https://github.com/EdVince/QRCode-NCNN)**: This project was inspired by `QRCode-NCNN`. While `QRCode-NCNN` replaced the DNN inference backend with ncnn, this project goes a step further by using ncnn's `simpleocv` to completely remove the OpenCV dependency, resulting in a more streamlined library.

## License

[Apache License 2.0](LICENSE)
